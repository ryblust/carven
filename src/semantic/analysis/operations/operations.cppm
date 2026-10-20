module carven:semantic.analysis.operations;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :frontend.literal;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.generic;
import :semantic.semir.initialization;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.vector;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

enum class OperatorResult {
    Operand,
    Boolean,
    Mask,
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

auto type_supports_copy(ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool;

auto type_satisfies_capability(
    const ProgramDraft& draft,
    ConstructionTypeRef type,
    BuiltinCapability capability
) noexcept -> bool;

template<typename ElementCapability>
auto hash_type_support(const CanonicalType& type, ElementCapability&& element) noexcept -> bool {
    return type.value.visit([&](const auto& shape) noexcept {
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
            return builtin_is_integer(shape.kind)
                || shape.kind == BuiltinType::Bool
                || shape.kind == BuiltinType::Char
                || shape.kind == BuiltinType::String
                || shape.kind == BuiltinType::Str;
        } else if constexpr (std::same_as<Shape, ArrayTypeValue>) {
            return element(shape.element);
        } else if constexpr (std::same_as<Shape, TupleTypeValue>) {
            return std::ranges::all_of(shape.elements, element);
        } else {
            return false;
        }
    });
}

auto type_supports_hash(const CanonicalTypeStore& types, TypeID type) noexcept -> bool;

// Concrete aliases in rigid environments preserve the ordinary type identity.
auto unwrap_type_alias(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> ConstructionTypeRef;

auto type_formation_status(
    const ProgramDraft& draft,
    TypeFormationRequirementKind requirement,
    ConstructionTypeRef type
) noexcept -> TypeFormationStatus;

auto default_initialization(ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> DefaultInitialization;

struct StructureInitializer final {
    std::uint32_t declaration_index;
    std::optional<ASTExprID> expression;
    std::optional<Span> name;
};

auto select_structure_initializers(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTConstructionInitializer& source,
    std::span<const ConstructionStructField> fields
) noexcept -> AnalysisResult<std::vector<StructureInitializer>>;

struct RecordShape final {
    ConstructionStructRef owner;
    RecordKind kind;
    ProgramSpellingID name;
    std::vector<ConstructionStructField> fields;
};

auto record_shape(ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<RecordShape>;

struct EnumCaseShape final {
    ConstructionEnumCaseRef reference;
    ProgramSpellingID name;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};

struct EnumShape final {
    ConstructionEnumRef owner;
    std::vector<EnumCaseShape> cases;
};

// Nominal classification does not require the enum's cases to be available.
auto enum_owner(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionEnumRef>;
auto enum_shape(ProgramDraft& draft, ConstructionTypeRef type) noexcept -> std::optional<EnumShape>;
auto enum_case_count(const ProgramDraft& draft, ConstructionEnumCaseRef reference) noexcept
    -> std::size_t;

enum class SequenceShapeKind { Array, Slice, Vector };

struct SequenceShape final {
    SequenceShapeKind kind;
    ConstructionTypeRef element;
    std::optional<std::uint64_t> extent;
};

auto sequence_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<SequenceShape>;

// The extent is a normal-completion fact; obtaining the value may still execute.
auto sequence_extent(const ProgramDraft& draft, const SemanticExpression& source) noexcept
    -> std::optional<std::uint64_t>;

auto constant_index_in_bounds(
    const ProgramDraft& draft,
    std::optional<std::uint64_t> extent,
    std::optional<ConstantID> index
) noexcept -> std::optional<bool>;

auto array_element(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionTypeRef>;

auto array_literal_element(
    const ProgramDraft& draft,
    std::optional<ConstructionTypeRef> expected,
    std::size_t extent
) noexcept -> std::expected<std::optional<ConstructionTypeRef>, OperationDiagnostic>;

auto slice_element(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionTypeRef>;

auto make_array_type(
    ProgramDraft& draft,
    ConstructionTypeRef element,
    std::uint64_t extent
) noexcept -> ConstructionTypeRef;

auto make_slice_type(ProgramDraft& draft, ConstructionTypeRef element) noexcept
    -> ConstructionTypeRef;

auto make_pointer_type(
    ProgramDraft& draft,
    ConstructionTypeRef target,
    PointerAccess access
) noexcept -> ConstructionTypeRef;

auto operation_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionOperationTypeValue>;

auto make_operation_type(
    ProgramDraft& draft,
    ConstructionTypeRef success,
    ConstructionFailureRef failures
) noexcept -> ConstructionTypeRef;

auto make_tuple_type(ProgramDraft& draft, std::vector<ConstructionTypeRef> elements) noexcept
    -> ConstructionTypeRef;

auto tuple_elements(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<std::vector<ConstructionTypeRef>>;

auto semantic_operator(ASTPrefixOperator op) noexcept -> UnaryOperator;
auto semantic_operator(ASTBinaryOperator op) noexcept -> std::optional<BinaryOperator>;
auto binary_operator_requires_equality(ASTBinaryOperator op) noexcept -> bool;

auto numeric_operand_plan(const ASTView& ast, ASTExprID left, ASTExprID right) noexcept
    -> BinaryOperandPlan;

auto binary_operand_plan(const ASTView& ast, const ASTBinaryExpr& expression) noexcept
    -> BinaryOperandPlan;

auto operator_result_builtin(
    OperatorResult result,
    std::optional<BuiltinType> operand = std::nullopt
) noexcept -> std::optional<BuiltinType>;

auto select_contextual_numeric_type(
    const ProgramDraft& draft,
    TypeID inferred,
    std::optional<ConstructionTypeRef> expected,
    NumericSuffix suffix
) noexcept -> TypeID;

auto builtin_type_supports_equality(BuiltinType type) noexcept -> bool;

struct PointerShape final {
    ConstructionTypeRef target;
    PointerAccess access;
};

auto pointer_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<PointerShape>;

auto pointer_narrows(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target
) noexcept -> bool;

auto constrain_invariant_type(
    ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    ProgramOriginID origin
) noexcept -> bool;

auto type_shapes_compatible(
    const ProgramDraft& draft,
    ConstructionTypeRef left,
    ConstructionTypeRef right
) noexcept -> bool;

auto make_callable_view_type(
    ProgramDraft& draft,
    std::vector<ConstructionCallableParameter> parameters,
    ConstructionTypeRef result,
    ConstructionFailureRef failures
) noexcept -> ConstructionTypeRef;

// Inspects construction shapes; nominal members are checked at their storage boundary.
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

auto cast_uses_cpp(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target
) noexcept -> bool;

auto decide_cast(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    bool source_is_numeric_enum
) noexcept -> CastDecision;

auto decide_slice_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t arguments
) noexcept -> std::expected<std::optional<SliceIntrinsic>, OperationDiagnostic>;

auto decide_vector_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t arguments
) noexcept -> std::expected<std::optional<VectorIntrinsic>, OperationDiagnostic>;

auto decide_text_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t argument_count
) noexcept -> TextMethodDecision;

auto decide_text_property(std::string_view name) noexcept -> TextIntrinsicDecision;

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

auto decide_float_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t arguments
) noexcept -> std::expected<std::optional<FloatIntrinsic>, OperationDiagnostic>;
