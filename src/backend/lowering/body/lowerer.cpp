module carven:backend.lowering.body.lowerer.impl;

import :backend.generation.names;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.traversal;
import :semantic.semir;
import :support.invariant;
import std;

namespace body_lowering {

BodyLowerer::BodyLowerer(
    ModuleLowering& source_context,
    BodyID id,
    TargetBodyInputs target_inputs
) noexcept
    : context(source_context),
      body(context.semantic().bodies().body(id)),
      inputs(std::move(target_inputs)),
      names(context.make_callable_name_allocator()),
      parameter_bindings(body.inputs().parameters.begin(), body.inputs().parameters.end()),
      capture_bindings(body.inputs().captures.begin(), body.inputs().captures.end()) {
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
    visit_semantic_nodes(body.region(), [&](const SemIRExpression& expression) noexcept {
        const auto* take = std::get_if<SemTake<TypeID, FailureSetID>>(&expression.value);
        if (take == nullptr) {
            return;
        }
        const auto* binding = std::get_if<SemBinding>(&take->place->value);
        if (binding == nullptr) {
            invariant_violation("take source is not a complete owner binding");
        }
        taken_bindings.insert(binding->binding);
    });
    for (const auto binding : body.bindings()) {
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

auto BodyLowerer::finish() noexcept -> LoweredBody {
    auto statements = region(body.region(), {.use = ResultUse::Discard, .storage = std::nullopt});
    mark_unused(statements);
    auto used_parameters = std::vector<bool>();
    for (const auto binding : parameter_bindings) {
        used_parameters.push_back(used_bindings.contains(binding));
    }
    return {
        .statements = std::move(statements),
        .used_parameters = std::move(used_parameters),
        .uses_test_context = uses_test_context
    };
}

auto BodyLowerer::region(const SemIRRegion& source, ResultDestination result) noexcept
    -> std::vector<TargetStmt> {
    auto statements = std::vector<TargetStmt>();
    for (const auto& item : source.statements) {
        statement(item, statements);
    }
    if (source.result.has_value() && falls_through(statements)) {
        result_expression(*source.result, std::move(result), statements);
    }
    return statements;
}

} // namespace body_lowering
