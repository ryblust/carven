module carven:semantic.analysis.body.site;

import :semantic.analysis.body.context;
import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.structured;
import std;

class BodyExprSite final {
public:
    using Value = BuiltExpression;
    using Selection = SelectedExpression;

    struct OperandState final {
        BodyPendingFailures pending;
        bool completes;
    };

    static auto operand_state() noexcept -> OperandState;

    auto module_id() const noexcept -> ProgramModuleID;
    auto catalog() const noexcept -> AnalysisCatalogView;
    auto permits_pointer_narrowing() const noexcept -> bool;
    auto infer_type(Value& value, Span span) noexcept -> AnalysisResult<ConstructionTypeRef>;
    auto aggregate_cost(std::size_t count, Span span) noexcept -> AnalysisResult<void>;
    auto resolve_array_extent(ASTExprID expression) noexcept -> AnalysisTask<std::uint64_t>;

    auto consume_unexecuted_read(Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;

    auto complete_members(ConstructionTypeRef type, Span name, bool associated) noexcept -> void;

    auto representation_access(ConstructionStructRef owner, Span span) noexcept
        -> AnalysisResult<void>;
    auto associated_reference(ConstructionStructRef owner, Span name_span) noexcept
        -> AnalysisTask<Value>;

    auto standard_call(const ASTCallExpr& source, StandardDeclaration function, Span span) noexcept
        -> AnalysisTask<Value>;

    auto number_parse_call(const ASTCallExpr& source, BuiltinType number, Span span) noexcept
        -> AnalysisTask<Value>;

    auto resolve_number_parser(BuiltinType number, Span span) noexcept -> AnalysisTask<CallableID>;

    auto native_associated_reference(ConstructionTypeRef type, Span name_span, Span span) noexcept
        -> Selection;

    auto native_qualified_reference(const ASTMemberExpr& source, Span span) noexcept
        -> AnalysisResult<std::optional<Selection>>;

    auto associated_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        ConstructionStructRef owner,
        Span span
    ) noexcept -> AnalysisTask<Value>;

    auto read_argument(ASTExprID expression, std::optional<ConstructionTypeRef> expected) noexcept
        -> AnalysisTask<Value>;

    auto read_static_argument(
        ASTExprID expression,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Value>;

    auto consume_write(OperandState& state, Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;

    auto consume_take(OperandState& state, Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;

    auto consume_cold_owner(OperandState& state, Value value, AccessMode access, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;

    auto require_matching_type(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisResult<void>;

    auto consume_read(OperandState& state, Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;

    auto consume_read_argument(OperandState& state, Value value, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;

    auto finish_constructed(
        ConstructionTypeRef type,
        SemanticExpressionValue value,
        OperandState state,
        Span span,
        std::optional<ConstantID> known = std::nullopt
    ) noexcept -> AnalysisResult<Value>;

    auto cpp_construct(
        const ASTConstructionExpr& source,
        ConstructionTypeRef type,
        Span span
    ) noexcept -> AnalysisTask<Value>;

    explicit BodyExprSite(BodyElaborator& body, bool allow_pointer_narrowing) noexcept;
    auto draft() noexcept -> ProgramDraft&;
    auto syntax() const noexcept -> ASTView;
    auto observe_expression(ASTExprID expression, ConstructionTypeRef type) noexcept -> void;
    auto observe_function(FunctionID function, Span name) noexcept -> void;
    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure;

    auto evaluate_constant_expression(ASTExprID expression) noexcept
        -> AnalysisTask<std::optional<ConstantID>>;

    auto read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
        -> AnalysisTask<Value>;

    auto read_aggregate_element(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected,
        bool explicit_context
    ) noexcept -> AnalysisTask<Value>;

    auto type(const Value& value) const noexcept -> ConstructionTypeRef;
    auto known(const Value& value) const noexcept -> std::optional<ConstantID>;
    auto external(ConstructionTypeRef type) const noexcept -> bool;
    auto await_expression(const ASTPrefixExpr& source, Span span) noexcept -> AnalysisTask<Value>;
    auto dereference(const ASTPrefixExpr& source, Span span) noexcept -> AnalysisTask<Value>;
    auto supports_equality(ConstructionTypeRef type) noexcept -> bool;
    auto numeric_enum(ConstructionTypeRef type) noexcept -> bool;
    auto resolve_type(ASTTypeID type) noexcept -> AnalysisTask<ConstructionTypeRef>;

    auto resolve_generic_arguments(
        std::span<const ASTGenericArgument> arguments,
        std::span<const GenericParameterKind> policies
    ) noexcept -> AnalysisTask<GenericEnvironment>;

    auto resolve_construction_type(const ASTConstructionType& type) noexcept
        -> AnalysisTask<ConstructionTypeRef>;

    auto resolve_duration_type(Span span) noexcept -> AnalysisTask<TypeID>;
    auto constant(ConstantID constant, Span span) noexcept -> Value;
    auto enter_operand_execution(bool executed) noexcept -> BodyReferencePathGuard;

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
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(const ASTNameExpr& value, Span span, std::optional<ConstructionTypeRef>) noexcept
        -> AnalysisTask<Selection>;

    auto extension(
        const ASTTupleExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTTypeApplicationExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTTupleElementExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto finish_tuple_element(
        ConstructionTypeRef type,
        std::uint32_t index,
        Value receiver,
        Span span
    ) noexcept -> AnalysisResult<Value>;

    auto extension(
        const ASTArrayExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTArrayRepeatExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTConstructionExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTAccessExpr& value,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTIndexExpr& value,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTPropagationExpr& value,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTIfForm& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTLambdaExpr& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTMatchForm& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto extension(
        const ASTTryForm& value,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<Selection>;

    auto construction_requests() noexcept -> ConstructionRequests&;

    auto resolve_type_qualifier(ASTExprID id) noexcept
        -> AnalysisTask<std::optional<ConstructionTypeRef>>;

    auto resolve_enum_case(ConstructionTypeRef type, std::string_view name, Span span) noexcept
        -> AnalysisTask<ResolvedEnumCase>;

    auto interpret_enum_case(
        ConstructionTypeRef type,
        std::string_view name,
        Span name_span,
        std::span<const ASTExprID> arguments,
        Span span,
        bool called
    ) noexcept -> AnalysisTask<Value>;

    auto spelling(Span span) const noexcept -> std::string;
    auto invalid_type_qualifier(Span span) noexcept -> AnalysisResult<Value>;

    auto require_invariant_storage(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisResult<void>;

    auto convert_argument(Value& value, ConstructionTypeRef type, Span span) noexcept
        -> AnalysisResult<void>;

    auto enum_constructor(const ResolvedEnumCase& selected, Span span) noexcept -> Value;

    auto extension(
        const ASTInterpolationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisTask<Selection>;

    auto known_sequence_extent(const Value& value) const noexcept -> std::optional<std::uint64_t>;
    auto external_index(Value receiver, Value index, Span span) noexcept -> AnalysisResult<Value>;

    auto external_member(const ASTMemberExpr& source, Value receiver, Span span) noexcept
        -> AnalysisResult<Selection>;

    auto finish_index(
        ConstructionTypeRef type,
        bool array,
        IndexBoundsPolicy bounds,
        Value receiver,
        Value index,
        Span span
    ) noexcept -> AnalysisResult<Value>;

    auto observe_field(ProgramOriginID definition, Span name, ConstructionTypeRef type) noexcept
        -> void;

    auto finish_field(
        ConstructionTypeRef type,
        FieldProjection field,
        Value receiver,
        Span span
    ) noexcept -> AnalysisResult<Value>;

    auto member_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        Value operand,
        Span span
    ) noexcept -> AnalysisTask<Value>;

    auto call(
        const ASTCallExpr& source,
        Span span,
        std::optional<Selection> callee = std::nullopt
    ) noexcept -> AnalysisTask<Value>;

private:
    auto finish(
        ConstructionTypeRef type,
        SemanticExpressionValue&& value,
        std::optional<ConstantID> known,
        Span span,
        BodyPendingFailures pending,
        bool completes
    ) noexcept -> AnalysisResult<Value>;

    auto external_operation(
        CppOperation operation,
        Value operand,
        std::optional<ConstructionTypeRef> type,
        Span span
    ) noexcept -> AnalysisResult<Value>;

    BodyElaborator& body;
    bool allow_pointer_narrowing;
};
