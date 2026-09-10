module carven:backend.realization.realizer.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.decl;
import :backend.realization.realizer;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import std;

BodyRealizer::BodyRealizer(
    ModuleLowering& source_context,
    const BodyConstruction& construction,
    BodyRealizationInputs target_inputs
) noexcept
    : context(source_context),
      construction(construction),
      metadata(context.semantic().bodies().body(construction.body())),
      inputs(std::move(target_inputs)),
      names(context.make_callable_name_allocator()),
      parameter_bindings(metadata.inputs().parameters.begin(), metadata.inputs().parameters.end()),
      capture_bindings(metadata.inputs().captures.begin(), metadata.inputs().captures.end()) {
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
    for (const auto& value : construction.expression_values()) {
        if (const auto* take = std::get_if<SemTake>(&value.operation.value)) {
            const auto* binding = std::get_if<SemBinding>(&take->place->value);
            if (binding == nullptr) {
                invariant_violation("take source is not a complete owner binding");
            }
            taken_bindings.insert(binding->binding);
        }
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
    auto statements = region(construction.root(), LoweringDiscardResult {});

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
        finish_body_declarations(completed, inputs.parameters, inputs.captures);
    return {
        .statements = std::move(completed),
        .referenced_parameters = std::move(referenced_parameters),
        .uses_test_context = uses_test_context
    };
}

auto BodyRealizer::region(ConstructionRegionID id, const LoweringResultDestination& result) noexcept
    -> LoweringStmtBuilder {
    const auto& source = construction.region(id);
    auto statements = LoweringStmtBuilder();
    for (const auto& item : source.statements) {
        if (!statements.continues()) {
            break;
        }
        static_cast<void>(statements.accept(statement(item, id)));
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
