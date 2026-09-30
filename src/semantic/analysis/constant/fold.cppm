module carven:semantic.analysis.constant.fold;

import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.structured;
import std;

// Known values of aggregate construction and projection from known operands.
// Body construction and static specialization share these rules.
auto array_constant(ProgramDraft& draft, TypeID type, const SemArray& array) noexcept
    -> std::optional<ConstantID>;

auto struct_constant(ProgramDraft& draft, TypeID type, const SemStruct& structure) noexcept
    -> std::optional<ConstantID>;

auto field_constant(
    const ProgramDraft& draft,
    std::optional<ConstantID> source,
    std::size_t field_index
) noexcept -> std::optional<ConstantID>;

// A known position outside the sequence has no value; its runtime check remains.
auto element_constant(
    const ProgramDraft& draft,
    std::optional<ConstantID> sequence,
    std::optional<ConstantID> index
) noexcept -> std::optional<ConstantID>;

auto fold_constant_expression(ProgramDraft& draft, SemanticExpression& expression) noexcept
    -> AnalysisResult<void>;
