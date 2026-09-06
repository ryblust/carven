module carven:semantic.analysis.constant.proof;

import :frontend.ast.ids;
import :frontend.ast.storage;
import :semantic.analysis.diagnostics;
import :semantic.semir.constant;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.text;
import std;

struct ConstantNamedValue final {
    ConstructionTypeRef type;
    std::optional<ConstantID> constant;
};

struct ConstantEnumCase final {
    EnumCaseID id;
    EnumID owner;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};

struct ConstantExpressionEnvironment final {
    std::function<AnalysisResult<ConstantNamedValue>(std::string_view, Span)> resolve_name;
    std::function<AnalysisResult<std::optional<TypeID>>(ASTExprID)> resolve_enum_qualifier;
    std::function<AnalysisResult<ConstantEnumCase>(TypeID, std::string_view, Span)>
        resolve_enum_case;
    std::function<AnalysisResult<ConstructionTypeRef>(ASTTypeID)> resolve_type;
    std::function<bool(ConstructionTypeRef)> supports_equality;
    std::function<bool(TypeID)> is_numeric_enum;
};

auto prove_constant_expression(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> AnalysisResult<std::optional<ConstantFact>>;

auto prove_array_extent(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    ASTExprID expression
) noexcept -> AnalysisResult<std::uint64_t>;
