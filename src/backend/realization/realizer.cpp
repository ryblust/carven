module carven:backend.realization.realizer.impl;

import :backend.generation.names;
import :backend.lowering.body;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.composition;
import :backend.realization.decl;
import :backend.realization.pattern;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :source.provenance;
import :support.invariant;
import :support.task;
import std;

BodyRealizer::BodyRealizer(
    ModuleLowering& source_context,
    const BodyPreparation& preparation,
    BodyLoweringInputs target_inputs
) noexcept
    : context(source_context),
      preparation(preparation),
      metadata(preparation.body()),
      inputs(std::move(target_inputs)),
      names(context.make_callable_name_allocator()) {
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
                context.target().add_local(names.local_symbol(
                    context.semantic().provenance().spelling(binding.value.name),
                    binding.id.index(),
                    callable_scope
                ))
            );
        }
    }
}

auto BodyRealizer::finish() noexcept -> LoweredBody {
    auto statements = region(metadata.region(), LoweringDiscardResult {}).run();

    for (const auto& entry : statements.exits().entries) {
        const auto exit = entry.target;
        if (exit.identity != 0
            || (exit.kind != LoweringExitKind::FunctionReturn
                && exit.kind != LoweringExitKind::Failure
                && exit.kind != LoweringExitKind::Test
                && exit.kind != LoweringExitKind::Unreachable)) {
            invariant_violation("body contains an unreceived control exit");
        }
    }
    auto completed = std::move(statements).finish();
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
    };
}

auto BodyRealizer::region(
    const SemanticRegion& source,
    const LoweringResultDestination& result
) noexcept -> ContinuationTask<LoweringStmtBuilder> {
    auto statements = LoweringStmtBuilder();
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
