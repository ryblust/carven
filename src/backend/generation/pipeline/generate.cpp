module carven:backend.generate.impl;

import :backend.emit;
import :backend.generate;
import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.lower;
import :semantic.hir.ids;
import :support.invariant;
import std;

auto generate_target(const SemanticProgram& semantic, TargetGenerationRequest request) noexcept
    -> ArtifactSet {
    if (semantic.modules().empty()) {
        invariant_violation("target generation requires at least one semantic module");
    }
    const auto linkage_domain = derive_linkage_domain_id(request);
    const auto plan = TargetGenerationPlan::build(semantic, linkage_domain);
    auto artifacts = std::vector<GeneratedArtifact>();
    artifacts.reserve(semantic.modules().size() + plan.interface_components().size() + 1);
    for (const auto& component : plan.interface_components()) {
        artifacts.push_back(
            emit(lower_interface_component_unit(semantic, request.tests, plan, component))
        );
    }
    for (auto index = 0uz; index < semantic.modules().size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        artifacts.push_back(
            emit(lower_module_implementation_unit(semantic, request.tests, plan, module_id))
        );
    }
    if (request.tests == TestEmissionMode::DefaultRunner) {
        artifacts.push_back(emit(lower_test_entry_unit(semantic, request.tests, plan)));
    }
    return ArtifactSet(std::move(artifacts));
}
