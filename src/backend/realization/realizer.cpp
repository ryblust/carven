module carven:backend.realization.realizer.impl;

import :backend.generation.names;
import :backend.lowering.body;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.async;
import :backend.preparation.body;
import :backend.realization.composition;
import :backend.realization.decl;
import :backend.realization.pattern;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :source.provenance;
import :support.invariant;
import :support.task;
import std;

namespace {
struct NativeCoroutineQuery final {
    bool& found;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto NativeCoroutineQuery::enter_expression(
    const TargetExpr& expression,
    TargetExpressionRole
) noexcept -> bool {
    found |= std::holds_alternative<TargetCoAwaitExpr>(expression.value);
    return true;
}

auto NativeCoroutineQuery::enter_statement(const TargetStmt& statement) noexcept -> bool {
    found |= std::holds_alternative<TargetCoReturnStmt>(statement.value);
    return true;
}
} // namespace

BodyRealizer::BodyRealizer(
    ModuleLowering& source_context,
    const BodyPreparation& preparation,
    BodyLoweringInputs target_inputs,
    BodyRealizer* enclosing,
    std::optional<LoweringResultDestination> local_result
) noexcept
    : context(source_context),
      preparation(preparation),
      metadata(preparation.body()),
      inputs(std::move(target_inputs)),
      owned_names(
          enclosing == nullptr ? context.make_callable_name_allocator() : TargetNameAllocator {}
      ),
      names(enclosing == nullptr ? owned_names : enclosing->names),
      owned_fusion {.remaining = 256uz, .active = {}},
      fusion(enclosing == nullptr ? owned_fusion : enclosing->fusion) {
    if (enclosing == nullptr) {
        if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit)) {
            fusion.active.push_back(callable->callable_id);
        }
    }
    if (local_result) {
        local_result_exit = LocalResultExit {
            .result = *local_result,
            .exit = RegionExit {
                .label = names.fresh(TargetTemporaryNameKind::Region),
                .target = {LoweringExitKind::FunctionReturn, 0}
            }
        };
    }
    const auto& capture_bindings = metadata.inputs().captures;
    if (inputs.captures.size() != capture_bindings.size()) {
        invariant_violation("target captures do not match semantic body inputs");
    }
    for (const auto& [binding, local] : inputs.parameters) {
        binding_locals.emplace(binding, local);
        names.reserve(context.target().local_name(local).spelling());
        names.reserve(context.target().local_name(local).spelling(), callable_scope);
    }
    for (auto index = 0uz; index < capture_bindings.size(); ++index) {
        capture_names.emplace(capture_bindings[index], inputs.captures[index]);
        names.reserve(inputs.captures[index].spelling());
        names.reserve(inputs.captures[index].spelling(), callable_scope);
    }
    for (const auto binding : metadata.bindings()) {
        if (!binding_locals.contains(binding.id) && !capture_names.contains(binding.id)) {
            binding_locals.emplace(
                binding.id,
                context.target().add_local(
                    enclosing != nullptr
                        ? names.fresh(TargetTemporaryNameKind::Owner)
                        : names.local_symbol(
                              context.semantic().provenance().spelling(binding.value.name),
                              binding.id.index(),
                              callable_scope
                          )
                )
            );
        }
    }
    if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit)) {
        if (auto selected = prepare_tail_await_loop(context.semantic(), callable->callable_id)) {
            tail_loop = TailAwaitLoop {
                .selection = std::move(*selected),
                .slots = {},
                .next = exit_target(LoweringExitKind::Continue),
                .next_label = names.fresh(TargetTemporaryNameKind::Continue)
            };
            for (const auto& [binding, local] : inputs.parameters) {
                static_cast<void>(local);
                tail_loop->slots.push_back(fresh_local(TargetTemporaryNameKind::Owner));
                binding_locals.at(binding) = fresh_local(TargetTemporaryNameKind::Operand);
            }
        }
    }
}

auto BodyRealizer::finish() noexcept -> LoweredBody {
    auto statements = region(metadata.region(), LoweringDiscardResult {}).run();

    if (is_async() && statements.continues()) {
        const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
        const auto& signature = context.semantic().callable_signatures().signature(
            context.semantic().declarations().callable(callable->callable_id).signature
        );
        if (!context.is_void(signature.result)) {
            invariant_violation("async body has an undelivered success value");
        }
        emit_return(std::nullopt, statements);
    }
    if (tail_loop) {
        statements = wrap_tail_loop(std::move(statements));
    }
    for (const auto& entry : statements.exits().entries) {
        const auto exit = entry.target;
        if (exit.identity != 0
            || (exit.kind != LoweringExitKind::FunctionReturn
                && exit.kind != LoweringExitKind::Failure
                && exit.kind != LoweringExitKind::Cancelled
                && exit.kind != LoweringExitKind::Test
                && exit.kind != LoweringExitKind::Unreachable)) {
            invariant_violation("body contains an unreceived control exit");
        }
    }
    if (local_result_exit && statements.exits().contains(local_result_exit->exit.target)) {
        auto local = LoweringStmtBuilder();
        local.scope(std::move(statements));
        local.resume(
            local_result_exit->exit.label,
            TargetJumpRole::RegionExit,
            local_result_exit->exit.target
        );
        statements = std::move(local);
    }
    const auto continues = statements.continues();
    auto completed = std::move(statements).finish();
    if (pending_completion) {
        auto storage = LoweringStmtBuilder();
        declare_deferred(*pending_completion, false, storage, true);
        auto prefixed = std::move(storage).finish();
        prefixed.append_range(completed | std::views::as_rvalue);
        completed = std::move(prefixed);
    }
    realize_async_exits(completed);
    if (is_async() && !local_result_exit) {
        auto native_operation = false;
        // A syntax-free infinite body still needs a native coroutine marker.
        auto query = NativeCoroutineQuery {.found = native_operation};
        for (const auto& statement : completed) {
            static_cast<void>(traverse_target_statement(statement, query));
        }
        if (!native_operation) {
            completed.push_back(generated_statement(
                TargetCoReturnStmt {
                    .expression = call_expression(
                        static_member_expression(
                            completion_type(),
                            TargetIdentifier::from_spelling("cancelled")
                        ),
                        {}
                    )
                }
            ));
        }
    }
    auto parameters = std::vector<TargetLocalID>();
    for (const auto& [binding, local] : inputs.parameters) {
        static_cast<void>(binding);
        parameters.push_back(local);
    }
    auto referenced_parameters =
        finish_body_declarations(completed, parameters, mutable_owners, unused_initializers);
    return {
        .statements = std::move(completed),
        .referenced_parameters = std::move(referenced_parameters),
        .continues = continues,
    };
}

auto BodyRealizer::region(
    const SemanticRegion& source,
    const LoweringResultDestination& result,
    bool retain_async_scope
) noexcept -> ContinuationTask<LoweringStmtBuilder> {
    auto statements = LoweringStmtBuilder();
    const auto retained = async_scopes.size();
    begin_async_region(source, statements);
    for (const auto& item : source.statements) {
        if (!statements.continues()) {
            break;
        }
        if (!item.reachable) {
            continue;
        }
        static_cast<void>(statements.accept((co_await statement(item))));
    }
    if (statements.continues()) {
        if (source.result.has_value()) {
            if (!source.result_reachable) {
                if (!retain_async_scope) {
                    leave_async_scopes(retained);
                }
                co_return statements;
            }
            auto delivery = LoweringStmtBuilder();
            (co_await result_expression(*source.result, result, delivery, source.lifetime));
            // Tail owners are initialized, observed and delivered before this block exits.
            statements.scope(std::move(delivery));
        } else {
            deliver_result(LoweringCompleted {}, result, statements);
        }
    }
    if (!retain_async_scope) {
        if (statements.continues()) {
            close_async_scopes(retained, false, statements);
        }
        leave_async_scopes(retained);
    }
    co_return statements;
}

auto BodyRealizer::exit_target(LoweringExitKind kind) noexcept -> LoweringExitTarget {
    return {kind, next_exit++};
}

auto BodyRealizer::fallible(const SemanticExpression& source) const noexcept
    -> std::optional<FallibleCall> {
    const auto* call = std::get_if<SemCall>(&source.value);
    if (call != nullptr
        && (!context.semantic()
                 .failure_sets()
                 .failure_set(call->callee_failures.resolved())
                 .members.empty()
            || context.semantic().may_stop_test(*call))) {
        return FallibleCall {
            .failures = call->callee_failures.resolved(),
            .destination = current_failure
        };
    }
    return std::nullopt;
}

auto BodyRealizer::fresh_local(TargetTemporaryNameKind kind) noexcept -> TargetLocalID {
    return context.target().add_local(names.fresh(kind));
}
