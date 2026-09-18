module carven:backend.realization.realizer.impl;

import :backend.preparation.body;
import :backend.generation.names;
import :backend.lowering.body;
import :backend.lowering.constant;
import :backend.lowering.context;
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
    const auto& parameter_bindings = metadata.inputs().parameters;
    const auto& capture_bindings = metadata.inputs().captures;
    if (inputs.parameters.size() != parameter_bindings.size()
        || inputs.captures.size() != capture_bindings.size()) {
        invariant_violation("target inputs do not match semantic body inputs");
    }
    for (auto index = 0uz; index < parameter_bindings.size(); ++index) {
        binding_names.emplace(parameter_bindings[index], inputs.parameters[index]);
        names.reserve(inputs.parameters[index].spelling());
        names.reserve(inputs.parameters[index].spelling(), callable_scope);
    }
    for (auto index = 0uz; index < capture_bindings.size(); ++index) {
        binding_names.emplace(capture_bindings[index], inputs.captures[index]);
        names.reserve(inputs.captures[index].spelling());
        names.reserve(inputs.captures[index].spelling(), callable_scope);
    }
    for (const auto binding : metadata.bindings()) {
        if (!binding_names.contains(binding.id)) {
            const auto preferred =
                names.source(context.semantic().provenance().spelling(binding.value.name));
            binding_names.emplace(
                binding.id,
                names.local_symbol(preferred.spelling(), binding.id.index(), callable_scope)
            );
        }
    }
}

auto BodyRealizer::finish() noexcept -> LoweredBody {
    auto statements = region(metadata.region(), LoweringDiscardResult {});

    for (const auto exit : statements.exits().targets) {
        if (exit.identity != 0
            || (exit.kind != LoweringExitKind::FunctionReturn
                && exit.kind != LoweringExitKind::Failure
                && exit.kind != LoweringExitKind::Test
                && exit.kind != LoweringExitKind::Unreachable)) {
            invariant_violation("body contains an unreceived control exit");
        }
    }
    auto completed = std::move(statements).finish();
    auto referenced_parameters =
        finish_body_declarations(completed, inputs.parameters, inputs.captures, mutable_owners);
    return {
        .statements = std::move(completed),
        .referenced_parameters = std::move(referenced_parameters),
    };
}

auto BodyRealizer::region(
    const SemanticRegion& source,
    const LoweringResultDestination& result
) noexcept -> LoweringStmtBuilder {
    auto statements = LoweringStmtBuilder();
    for (const auto& item : source.statements) {
        if (!statements.continues()) {
            break;
        }
        static_cast<void>(statements.accept(statement(item)));
    }
    if (statements.continues()) {
        if (source.result.has_value()) {
            result_expression(*source.result, result, statements);
        } else {
            deliver_result(LoweringCompleted {}, result, statements);
        }
    }
    return statements;
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
            || context.semantic().may_stop_test(call->callee->type.resolved()))) {
        return FallibleCall {
            .failures = call->callee_failures.resolved(),
            .destination = current_failure
        };
    }
    return std::nullopt;
}
