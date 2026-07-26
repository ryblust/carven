module carven:backend.lower;

import :backend.target;
import :backend.generation.linkage;
import :backend.generation.plan;
import :compilation.request;
import :semantic.hir;

auto lower_interface_component_unit(
    const SemanticProgram& semantic,
    TestEmissionMode test_mode,
    const TargetGenerationPlan& plan,
    const TargetInterfaceComponentPlan& component
) noexcept -> TargetUnit;
auto lower_module_implementation_unit(
    const SemanticProgram& semantic,
    TestEmissionMode test_mode,
    const TargetGenerationPlan& plan,
    ProgramModuleID module_id
) noexcept -> TargetUnit;
auto lower_test_entry_unit(
    const SemanticProgram& semantic,
    TestEmissionMode test_mode,
    const TargetGenerationPlan& plan
) noexcept -> TargetUnit;
