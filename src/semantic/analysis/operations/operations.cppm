module carven:semantic.analysis.operations;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :frontend.literal;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.type;
import std;

enum class OperatorResult {
    Operand,
    Boolean,
};

struct OperationDiagnostic final {
    std::string_view message;
    DiagnosticCode code;
};

enum class BinaryOperandPlan {
    Independent,
    LeftExpectedFromRight,
    RightExpectedFromLeft,
};

using OperatorDecision = std::expected<OperatorResult, OperationDiagnostic>;
using CastDecision = std::expected<CastKind, OperationDiagnostic>;
using TextMethodDecision = std::expected<std::optional<TextIntrinsic>, OperationDiagnostic>;
using TextIntrinsicDecision = std::expected<TextIntrinsic, OperationDiagnostic>;

auto semantic_operator(ASTPrefixOperator op) noexcept -> UnaryOperator;
auto semantic_operator(ASTBinaryOperator op) noexcept -> std::optional<BinaryOperator>;
auto binary_operator_requires_equality(ASTBinaryOperator op) noexcept -> bool;
auto binary_operand_plan(const ASTView& ast, const ASTBinaryExpr& expression) noexcept
    -> BinaryOperandPlan;

auto operator_result_builtin(OperatorResult result) noexcept -> std::optional<BuiltinType>;
auto select_contextual_numeric_type(
    const ProgramDraft& draft,
    TypeID inferred,
    std::optional<ConstructionTypeRef> expected,
    NumericSuffix suffix
) noexcept -> TypeID;

auto builtin_type_supports_equality(BuiltinType type) noexcept -> bool;
auto pointer_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<PointerTypeValue>;
auto pointer_narrows(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target
) noexcept -> bool;

auto type_shapes_compatible(
    const ProgramDraft& draft,
    ConstructionTypeRef left,
    ConstructionTypeRef right
) noexcept -> bool;
auto type_contains_callable_view(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> bool;
auto type_supports_equality(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool;

auto decide_unary_operator(
    const ProgramDraft& draft,
    UnaryOperator op,
    ConstructionTypeRef operand
) noexcept -> OperatorDecision;

auto decide_binary_operator(
    const ProgramDraft& draft,
    BinaryOperator op,
    ConstructionTypeRef left,
    ConstructionTypeRef right,
    bool operands_compatible,
    bool equality_capable
) noexcept -> OperatorDecision;

auto decide_binary_operator(
    const ProgramDraft& draft,
    ASTBinaryOperator op,
    ConstructionTypeRef left,
    ConstructionTypeRef right,
    bool operands_compatible,
    bool equality_capable
) noexcept -> OperatorDecision;

auto decide_cast(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    bool source_is_numeric_enum
) noexcept -> CastDecision;

auto decide_text_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t argument_count
) noexcept -> TextMethodDecision;
auto decide_text_property(std::string_view name) noexcept -> TextIntrinsicDecision;
auto text_intrinsic_result(TextIntrinsic intrinsic) noexcept -> BuiltinType;

auto decide_unary_operator(
    const CanonicalTypeStore& types,
    UnaryOperator operation,
    TypeID operand
) noexcept -> OperatorDecision;
auto decide_binary_operator(
    const CanonicalTypeStore& types,
    BinaryOperator operation,
    TypeID left,
    TypeID right,
    bool compatible,
    bool equality
) noexcept -> OperatorDecision;
auto decide_cast(
    const CanonicalTypeStore& types,
    TypeID source,
    TypeID target,
    bool numeric_enum
) noexcept -> CastDecision;
auto type_supports_equality(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations,
    TypeID type
) noexcept -> bool;
