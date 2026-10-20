module carven:backend.realization.async_exits.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.stmt;
import :backend.target.traversal;
import :support.invariant;
import std;

namespace {

struct NativeContinue final {
    const TargetIdentifier& label;
    std::vector<std::size_t>& pending;

    auto enter_statement(TargetStmt& statement) const noexcept -> bool;
};

auto NativeContinue::enter_statement(TargetStmt& statement) const noexcept -> bool {
    if (const auto* jump = std::get_if<TargetGotoStmt>(&statement.value);
        jump != nullptr && jump->label == label) {
        statement.value = TargetContinueStmt {};
    } else if (const auto* exit = std::get_if<TargetPendingExitStmt>(&statement.value)) {
        pending.push_back(exit->identity);
    }
    return true;
}

} // namespace

auto BodyRealizer::resolve_native_continue(
    std::span<TargetStmt> statements,
    const TargetIdentifier& label
) noexcept -> void {
    auto pending = std::vector<std::size_t>();
    const auto continuation = NativeContinue {.label = label, .pending = pending};
    if (!traverse_target_statements(statements, continuation)) {
        invariant_violation("native continue resolution did not complete");
    }
    // Follow pending cleanup edges to their final transfers, including stops
    // retained by another cleanup continuation. Each pending node owns one edge.
    while (!pending.empty()) {
        const auto identity = pending.back();
        pending.pop_back();
        if (!traverse_target_statement(async_exits.at(identity).continuation, continuation)) {
            invariant_violation("deferred native continue resolution did not complete");
        }
    }
}

auto BodyRealizer::emit_async_exit(
    TargetStmt continuation,
    LoweringExitTarget target,
    std::vector<TargetLocalID> closing_scopes,
    bool cancel,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (closing_scopes.empty()) {
        destination.terminate(std::move(continuation), target);
        return;
    }
    if (!std::holds_alternative<TargetCoReturnStmt>(continuation.value)) {
        // Cleanup can suspend. Continue within the invocation only if it survived;
        // a stopped path closes the scopes retained by the original transfer.
        auto active = std::exchange(async_scopes, {});
        for (const auto& scope : active) {
            if (!std::ranges::contains(closing_scopes, scope.scope)) {
                async_scopes.push_back(scope);
            }
        }
        auto resumed = LoweringStmtBuilder();
        check_stopped(resumed);
        async_scopes = std::move(active);
        resumed.terminate(std::move(continuation), target);
        destination.record_exits(resumed.exits());
        continuation =
            generated_statement(TargetBlockStmt {.statements = std::move(resumed).finish()});
    }
    const auto identity = async_exits.size();
    async_exits.emplace(
        identity,
        AsyncExit {
            .label = std::nullopt,
            .closing_scopes = std::move(closing_scopes),
            .cancel = cancel,
            .continuation = std::move(continuation),
        }
    );
    destination.terminate(
        generated_statement(TargetPendingExitStmt {.identity = identity}),
        target
    );
}

auto BodyRealizer::realize_async_exits(std::vector<TargetStmt>& statements) noexcept -> void {
    if (async_exits.empty()) {
        return;
    }
    const auto expand = [&](AsyncExit exit,
                            std::span<const TargetLocalID> locals,
                            std::vector<TargetStmt>& output,
                            std::vector<AsyncExit>& pending) noexcept {
        auto count = 0uz;
        while (count < exit.closing_scopes.size()
               && std::ranges::contains(locals, exit.closing_scopes[count])) {
            ++count;
        }
        auto closure = LoweringStmtBuilder();
        close_async_locals(std::span(exit.closing_scopes).first(count), exit.cancel, closure);
        output.append_range(std::move(closure).finish() | std::views::as_rvalue);
        exit.closing_scopes.erase(exit.closing_scopes.begin(), exit.closing_scopes.begin() + count);
        if (exit.closing_scopes.empty()) {
            output.push_back(std::move(exit.continuation));
            return;
        }
        exit.label = names.fresh(TargetTemporaryNameKind::Region);
        output.push_back(generated_statement(TargetGotoStmt {.label = *exit.label}));
        pending.push_back(std::move(exit));
    };
    const auto realize = [&](
                             this const auto& self,
                             std::vector<TargetStmt>& source,
                             std::span<const TargetLocalID> inherited
                         ) noexcept -> ContinuationTask<std::vector<AsyncExit>> {
        auto output = std::vector<TargetStmt>();
        auto locals = std::vector<TargetLocalID>();
        auto pending = std::vector<AsyncExit>();
        for (auto& statement : source) {
            if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
                variable != nullptr) {
                locals.push_back(variable->local);
            }
            // Local storage must leave this scope before inherited child
            // scopes close. Earlier exits cross no such storage.
            const auto visible =
                locals.empty() ? inherited : std::span<const TargetLocalID>(locals);
            if (const auto* exit = std::get_if<TargetPendingExitStmt>(&statement.value)) {
                const auto node = async_exits.extract(exit->identity);
                if (node.empty()) {
                    invariant_violation("async exit has no realization record");
                }
                expand(std::move(node.mapped()), visible, output, pending);
                continue;
            }
            auto incoming = std::vector<AsyncExit>();
            co_await statement.value.visit(
                [&](auto& value) noexcept -> ContinuationTask<std::monostate> {
                    using Value = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<Value, TargetBlockStmt>) {
                        incoming = co_await self(value.statements, visible);
                    } else if constexpr (std::same_as<Value, TargetIfStmt>) {
                        incoming = co_await self(value.body, visible);
                        if (value.else_body) {
                            incoming.append_range(
                                (co_await self(*value.else_body, visible)) | std::views::as_rvalue
                            );
                        }
                    } else if constexpr (std::same_as<Value, TargetWhileStmt>
                                         || std::same_as<Value, TargetForStmt>
                                         || std::same_as<Value, TargetRangeForStmt>) {
                        incoming = co_await self(value.body, {});
                    }
                    co_return {};
                }
            );
            output.push_back(std::move(statement));
            if (incoming.empty()) {
                continue;
            }
            // Place relays immediately after their child statement, before any
            // later initialization. Native scope exit destroys child storage
            // before a relay can suspend while closing this enclosing scope.
            // Transparent lists pass relays to the owning scope together.
            auto done = std::optional<TargetIdentifier>();
            for (auto& exit : incoming) {
                if (!std::ranges::contains(locals, exit.closing_scopes.front())) {
                    pending.push_back(std::move(exit));
                    continue;
                }
                if (!done) {
                    done = names.fresh(TargetTemporaryNameKind::Region);
                    output.push_back(generated_statement(TargetGotoStmt {.label = *done}));
                }
                output.push_back(generated_statement(TargetLabelStmt {.label = *exit.label}));
                expand(std::move(exit), locals, output, pending);
            }
            if (done) {
                output.push_back(generated_statement(TargetLabelStmt {.label = *done}));
            }
        }
        source = std::move(output);
        co_return pending;
    };
    // A continuation can itself stop after cleanup and retain an enclosing exit.
    while (!async_exits.empty()) {
        const auto count = async_exits.size();
        const auto remaining = realize(statements, {}).run();
        if (!remaining.empty() || async_exits.size() >= count) {
            invariant_violation("async exit did not reach its owning child scope");
        }
    }
}
