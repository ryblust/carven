module carven:semantic.analysis.body.expression_site;

import :semantic.analysis.body.context;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.structured;
import :support.invariant;
import std;

class BodyExpressionSite final {
public:
    using Value = BuiltExpression;
    using Result = SelectedExpression;

    explicit BodyExpressionSite(BodyElaborator& body, bool allow_pointer_narrowing = true) noexcept;
    auto draft() noexcept -> ProgramDraft&;
    auto syntax() const noexcept -> ASTView;
    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure;
    auto read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
        -> AnalysisResult<Value>;
    auto present(const Value&) const noexcept -> bool;
    auto unavailable() const noexcept -> Value;
    auto type(const Value& value) const noexcept -> ConstructionTypeRef;
    auto known(const Value& value) const noexcept -> std::optional<ConstantID>;
    auto external(ConstructionTypeRef type) const noexcept -> bool;
    auto dereference(const ASTPrefixExpr& source, Span span) noexcept -> AnalysisResult<Value>;
    auto supports_equality(ConstructionTypeRef type) noexcept -> bool;
    auto numeric_enum(ConstructionTypeRef type) noexcept -> bool;
    auto resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef>;
    auto c_string(std::string_view bytes, Span span) noexcept -> Value;
    auto constant(ConstantID constant, Span span) noexcept -> Value;
    auto enter_operand_execution(bool executed) noexcept -> BodyReferencePathGuard;
    auto finish_unary(
        UnaryOperator operation,
        ConstructionTypeRef type,
        Value operand,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto finish_binary(
        BinaryOperator operation,
        ConstructionTypeRef type,
        Value left,
        Value right,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto finish_cast(
        CastKind kind,
        ConstructionTypeRef target,
        Value operand,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto finish_short_circuit(
        bool conjunction,
        Value left,
        Value right,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto external_unary(UnaryOperator operation, Value operand, Span span) noexcept
        -> AnalysisResult<Value>;
    auto external_cast(ConstructionTypeRef type, Value operand, Span span) noexcept
        -> AnalysisResult<Value>;
    auto external_binary(BinaryOperator operation, Value left, Value right, Span span) noexcept
        -> AnalysisResult<Value>;
    auto extension(
        const ASTCppNameExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTNameExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTArrayExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTConstructionExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTAccessExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTIndexExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTPropagationExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTIfForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTLambdaExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTMatchForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto extension(
        const ASTTryForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result>;
    auto resolve_name(std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedConstantName>;
    auto resolve_enum_qualifier(ASTExprID id) noexcept -> AnalysisResult<std::optional<TypeID>>;
    auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedEnumCase>;
    auto is_numeric_enum(TypeID type) noexcept -> bool;
    auto admits(const ASTExpr&) const noexcept -> bool;
    auto spelling(Span span) const noexcept -> std::string;
    auto invalid_enum_qualifier(Span span) noexcept -> AnalysisResult<Value>;
    auto convert_argument(Value& value, ConstructionTypeRef type, Span span) noexcept
        -> AnalysisResult<void>;
    auto enum_constructor(
        TypeID enumeration_type,
        const ResolvedEnumCase& selected,
        Span span
    ) noexcept -> Value;
    auto finish_enum_case(
        TypeID type,
        EnumCaseID selected,
        std::vector<Value> arguments,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto extension(
        const ASTInterpolationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisResult<Result>;
    auto finish_text(
        TextIntrinsic intrinsic,
        TypeID type,
        Value operand,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto finish_slice_call(
        SliceIntrinsic intrinsic,
        Value receiver,
        std::span<const ASTCallArgument> arguments,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto finish_text_call(
        TextIntrinsic intrinsic,
        std::optional<Value> receiver,
        std::span<const ASTCallArgument> arguments,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto member(const ASTMemberExpr& source, Value operand, Span span) noexcept
        -> AnalysisResult<Result>;
    auto member_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        Value operand,
        Span span
    ) noexcept -> AnalysisResult<Value>;
    auto call(const ASTCallExpr& source, Span span) noexcept -> AnalysisResult<Value>;

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
    ) noexcept -> AnalysisResult<Value>;

    BodyElaborator& body;
    bool allow_pointer_narrowing;
};
