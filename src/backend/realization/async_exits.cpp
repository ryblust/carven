module carven:backend.realization.async_exits.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.stmt;
import :support.invariant;
import std;

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
    const auto label = names.fresh(TargetTemporaryNameKind::Region);
    async_exits.emplace(
        std::string(label.spelling()),
        AsyncExit {
            .label = label,
            .closing_scopes = std::move(closing_scopes),
            .cancel = cancel,
            .continuation = std::move(continuation),
        }
    );
    destination.terminate(
        generated_statement(TargetGotoStmt {.label = label, .role = TargetJumpRole::RegionExit}),
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
        output.push_back(generated_statement(
            TargetGotoStmt {.label = exit.label, .role = TargetJumpRole::RegionExit}
        ));
        pending.push_back(std::move(exit));
    };
    const auto realize =
        [&](this const auto& self,
            std::vector<TargetStmt>& source) noexcept -> ContinuationTask<std::vector<AsyncExit>> {
        auto output = std::vector<TargetStmt>();
        auto locals = std::vector<TargetLocalID>();
        auto pending = std::vector<AsyncExit>();
        for (auto& statement : source) {
            if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value);
                variable != nullptr) {
                locals.push_back(variable->local);
            }
            if (const auto* jump = std::get_if<TargetGotoStmt>(&statement.value)) {
                if (auto node = async_exits.extract(std::string(jump->label.spelling()));
                    !node.empty()) {
                    expand(std::move(node.mapped()), locals, output, pending);
                    continue;
                }
            }
            auto incoming = std::vector<AsyncExit>();
            co_await statement.value.visit(
                [&](auto& value) noexcept -> ContinuationTask<std::monostate> {
                    using Value = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<Value, TargetBlockStmt>) {
                        incoming = co_await self(value.statements);
                    } else if constexpr (std::same_as<Value, TargetIfStmt>) {
                        for (auto& branch : value.branches) {
                            incoming.append_range(
                                (co_await self(branch.body)) | std::views::as_rvalue
                            );
                        }
                        if (value.else_body) {
                            incoming.append_range(
                                (co_await self(*value.else_body)) | std::views::as_rvalue
                            );
                        }
                    } else if constexpr (std::same_as<Value, TargetWhileStmt>
                                         || std::same_as<Value, TargetForStmt>
                                         || std::same_as<Value, TargetRangeForStmt>) {
                        incoming = co_await self(value.body);
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
            auto done = std::optional<TargetIdentifier>();
            for (auto& exit : incoming) {
                if (!std::ranges::contains(locals, exit.closing_scopes.front())) {
                    pending.push_back(std::move(exit));
                    continue;
                }
                if (!done) {
                    done = names.fresh(TargetTemporaryNameKind::Region);
                    output.push_back(generated_statement(
                        TargetGotoStmt {.label = *done, .role = TargetJumpRole::RegionExit}
                    ));
                }
                output.push_back(generated_statement(
                    TargetLabelStmt {.label = exit.label, .role = TargetJumpRole::RegionExit}
                ));
                expand(std::move(exit), locals, output, pending);
            }
            if (done) {
                output.push_back(generated_statement(
                    TargetLabelStmt {.label = *done, .role = TargetJumpRole::RegionExit}
                ));
            }
        }
        source = std::move(output);
        co_return pending;
    };
    const auto remaining = realize(statements).run();
    if (!remaining.empty() || !async_exits.empty()) {
        invariant_violation("async exit did not reach its owning child scope");
    }
}
