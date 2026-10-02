module carven:semantic.analysis.constant.freeze;

import :semantic.analysis.program;
import :semantic.evaluation.value;
import std;

// Retains a completed execution result in the receiving program's constant store.
auto freeze_constant_value(ProgramDraft& draft, ExecutionValue value) noexcept
    -> std::optional<ConstantID>;

// A local initializer freezes owning text at its outer boundary. Static call
// inputs instead preserve their declared types.
auto constant_initializer_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> ConstructionTypeRef;
