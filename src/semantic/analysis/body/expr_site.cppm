module carven:semantic.analysis.body.expr_site;

import :semantic.analysis.body.context;
import :semantic.analysis.construction.requests;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.structured;
import :support.invariant;
import std;

// Ordinary body construction admits every source form; only diagnosed failure
// may cross back into the body's analysis pipeline.
template<typename Value>
auto require_body_expression(ExpressionResult<Value> result) noexcept -> AnalysisResult<Value> {
    if (!result) {
        if (const auto* diagnostic = std::get_if<AnalysisFailure>(&result.error())) {
            return std::unexpected(*diagnostic);
        }
        invariant_violation("ordinary body expression was not admitted");
    }
    if constexpr (std::is_void_v<Value>) {
        return {};
    } else {
        return std::move(*result);
    }
}

class BodyExprSite final {
public:
    using Value = BuiltExpression;
    using Selection = SelectedExpression;
    static constexpr auto mode = ExpressionMode::Body;

    struct OperandState final {
        BodyPendingFailureTerms pending;
        bool completes;
    };

    static auto operand_state() noexcept -> OperandState;

    auto module_id() const noexcept -> ProgramModuleID;
    auto permits_pointer_narrowing() const noexcept -> bool;
    auto infer_type(Value& value, Span span) noexcept -> ExpressionResult<ConstructionTypeRef>;
    auto aggregate_cost(std::size_t count, Span span) const noexcept -> ExpressionResult<void>;
    auto aggregate_admitted(ConstructionTypeRef type, Span span) const noexcept
        -> ExpressionResult<bool>;
    auto read_argument(ASTExprID expression, std::optional<ConstructionTypeRef> expected) noexcept
        -> ExpressionTask<Value>;
    auto consume_write(OperandState& state, Value value, Span span) noexcept
        -> ExpressionResult<SemanticExpression>;
    auto consume_read(OperandState& state, Value value, Span span) noexcept
        -> ExpressionResult<SemanticExpression>;
    auto finish_constructed(
        ConstructionTypeRef type,
        SemanticExpressionValue value,
        OperandState state,
        Span span,
        std::optional<ConstantID> known = std::nullopt
    ) noexcept -> Value;
    auto cpp_construct(
        const ASTConstructionExpr& source,
        ConstructionTypeRef type,
        Span span
    ) noexcept -> ExpressionTask<Value>;

    explicit BodyExprSite(BodyElaborator& body, bool allow_pointer_narrowing = true) noexcept;
    auto draft() noexcept -> ProgramDraft&;
    auto syntax() const noexcept -> ASTView;
    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure;
    auto read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
        -> ExpressionTask<Value>;
    auto read_array_element(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected,
        bool explicit_context
    ) noexcept -> ExpressionTask<Value>;
    auto type(const Value& value) const noexcept -> ConstructionTypeRef;
    auto known(const Value& value) const noexcept -> std::optional<ConstantID>;
    auto external(ConstructionTypeRef type) const noexcept -> bool;
    auto dereference(const ASTPrefixExpr& source, Span span) noexcept -> ExpressionTask<Value>;
    auto supports_equality(ConstructionTypeRef type) noexcept -> bool;
    auto numeric_enum(ConstructionTypeRef type) noexcept -> bool;
    auto resolve_type(ASTTypeID type) noexcept -> ExpressionTask<ConstructionTypeRef>;
    auto resolve_construction_type(const ASTConstructionType& type) noexcept
        -> ExpressionTask<ConstructionTypeRef>;
    auto c_string(std::string_view bytes, Span span) noexcept -> Value;
    auto constant(ConstantID constant, Span span) noexcept -> Value;
    auto enter_operand_execution(bool executed) noexcept -> BodyReferencePathGuard;
    auto finish_short_circuit(
        bool conjunction,
        Value left,
        Value right,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> ExpressionResult<Value>;
    auto external_unary(UnaryOperator operation, Value operand, Span span) noexcept
        -> ExpressionResult<Value>;
    auto external_cast(ConstructionTypeRef type, Value operand, Span span) noexcept
        -> ExpressionResult<Value>;
    auto external_binary(BinaryOperator operation, Value left, Value right, Span span) noexcept
        -> ExpressionResult<Value>;
    auto extension(
        const ASTCppNameExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTNameExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTArrayExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTConstructionExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTAccessExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTIndexExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTPropagationExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTIfForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTLambdaExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTMatchForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto extension(
        const ASTTryForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> ExpressionTask<Selection>;
    auto resolve_name(std::string_view name, Span span) noexcept
        -> ExpressionTask<std::optional<ConstantID>>;
    auto construction_requests() noexcept -> ConstructionRequests&;
    auto resolve_function(std::string_view name, Span span) noexcept
        -> ExpressionTask<std::optional<FunctionID>>;
    auto resolve_enum_qualifier(ASTExprID id) noexcept -> ExpressionTask<std::optional<TypeID>>;
    auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> ExpressionTask<ResolvedEnumCase>;
    auto is_numeric_enum(TypeID type) noexcept -> bool;
    auto admits(const ASTExpr&) const noexcept -> bool;
    auto spelling(Span span) const noexcept -> std::string;
    auto invalid_enum_qualifier(Span span) noexcept -> ExpressionResult<Value>;
    auto require_invariant_storage(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> ExpressionResult<void>;
    auto convert_argument(Value& value, ConstructionTypeRef type, Span span) noexcept
        -> ExpressionResult<void>;
    auto enum_constructor(
        TypeID enumeration_type,
        const ResolvedEnumCase& selected,
        Span span
    ) noexcept -> Value;
    auto extension(
        const ASTInterpolationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> ExpressionTask<Selection>;
    auto known_sequence_extent(const Value& value) const noexcept -> std::optional<std::uint64_t>;
    auto external_index(Value receiver, Value index, Span span) noexcept -> ExpressionResult<Value>;
    auto external_member(const ASTMemberExpr& source, Value receiver, Span span) noexcept
        -> ExpressionResult<Selection>;
    auto finish_index(
        ConstructionTypeRef type,
        bool array,
        IndexBoundsPolicy bounds,
        Value receiver,
        Value index,
        Span span
    ) noexcept -> ExpressionResult<Value>;
    auto finish_field(
        ConstructionTypeRef type,
        FieldProjection field,
        Value receiver,
        Span span
    ) noexcept -> ExpressionResult<Value>;
    auto member_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        Value operand,
        Span span
    ) noexcept -> ExpressionTask<Value>;
    auto call(const ASTCallExpr& source, Span span) noexcept -> ExpressionTask<Value>;

private:
    auto finish(
        ConstructionTypeRef type,
        SemanticExpressionValue value,
        std::optional<ConstantID> known,
        Span span,
        BodyPendingFailureTerms pending,
        bool completes
    ) noexcept -> Value;
    auto external_operation(
        CppOperation operation,
        Value operand,
        std::optional<ConstructionTypeRef> type,
        Span span
    ) noexcept -> ExpressionResult<Value>;

    BodyElaborator& body;
    bool allow_pointer_narrowing;
};
