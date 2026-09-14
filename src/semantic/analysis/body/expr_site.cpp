module carven:semantic.analysis.body.expr_site.impl;

import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.construction.requests;
import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.interpolation;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.projection;
import :semantic.analysis.expr.text;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :support.invariant;
import std;

BodyExprSite::BodyExprSite(BodyElaborator& body, bool allow_pointer_narrowing) noexcept
    : body(body),
      allow_pointer_narrowing(allow_pointer_narrowing) {}

auto BodyExprSite::draft() noexcept -> ProgramDraft& {
    return body.draft();
}

auto BodyExprSite::construction_requests() noexcept -> ConstructionRequests& {
    return body.construction_requests();
}

auto BodyExprSite::resolve_function(std::string_view name, Span span) noexcept
    -> ExpressionResult<std::optional<FunctionID>> {
    return body.resolve_function(name, span);
}

auto BodyExprSite::syntax() const noexcept -> ASTView {
    return body.ast;
}

auto BodyExprSite::fail(Span span, DiagnosticCode code, std::string message) noexcept
    -> AnalysisFailure {
    return body.fail(span, code, std::move(message));
}

auto BodyExprSite::read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
    -> ExpressionResult<Value> {
    return body.expression(id, expected, allow_pointer_narrowing);
}

auto BodyExprSite::type(const Value& value) const noexcept -> ConstructionTypeRef {
    return value.type();
}

auto BodyExprSite::known(const Value& value) const noexcept -> std::optional<ConstantID> {
    return value.constant();
}

auto BodyExprSite::external(ConstructionTypeRef type) const noexcept -> bool {
    return body.is_cpp_type(type);
}

auto BodyExprSite::dereference(const ASTPrefixExpr& source, Span span) noexcept
    -> ExpressionResult<Value> {
    return body.dereference_expression(source, span);
}

auto BodyExprSite::supports_equality(ConstructionTypeRef type) noexcept -> bool {
    return type_supports_equality(draft(), type);
}

auto BodyExprSite::numeric_enum(ConstructionTypeRef type) noexcept -> bool {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        return false;
    }
    const auto canonical = draft().type_copy(*concrete);
    const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value);
    return enumeration != nullptr
        && std::holds_alternative<NumericEnumRepresentation>(
               draft().enum_declaration_copy(enumeration->enumeration).representation
        );
}

auto BodyExprSite::resolve_construction_type(const ASTConstructionType& type) noexcept
    -> ExpressionResult<ConstructionTypeRef> {
    return body.resolve_construction_type(type);
}

auto BodyExprSite::resolve_type(ASTTypeID type) noexcept -> ExpressionResult<ConstructionTypeRef> {
    return body.resolve_type(type);
}

auto BodyExprSite::c_string(std::string_view bytes, Span span) noexcept -> Value {
    const auto type =
        draft().intern_type({.value = CppTypeValue {.form = CppConstCharPointerType {}}});
    return body.make_built(
        type,
        SemCpp {
            .operation = CppCStringOperation {.bytes = std::string(bytes)},
            .operands = {},
        },
        span
    );
}

auto BodyExprSite::constant(ConstantID constant, Span span) noexcept -> Value {
    return body
        .make_built(draft().constant(constant).type, SemConstant {.constant = constant}, span);
}

auto BodyExprSite::enter_operand_execution(bool executed) noexcept -> BodyReferencePathGuard {
    return BodyReferencePathGuard(body.reference_path_reachable, executed);
}

auto BodyExprSite::finish_short_circuit(
    bool conjunction,
    Value left,
    Value right,
    std::optional<ConstantID> known,
    Span span
) noexcept -> ExpressionResult<Value> {
    auto pending = take_pending_failures(left);
    append_pending_failures(pending, take_pending_failures(right));
    const auto& truth = known_boolean_constant(draft(), left.constant());
    const auto completes = left.completes && (truth == !conjunction || right.completes);
    auto first = body.consume_value(left, span, AccessMode::Read);
    if (!first.has_value()) {
        return std::unexpected(first.error());
    }
    auto second = body.consume_value(right, span, AccessMode::Read);
    if (!second.has_value()) {
        return std::unexpected(second.error());
    }
    return finish(
        draft().intern_builtin_type(BuiltinType::Bool),
        SemShortCircuit {
            UniqueIndirect(std::move(*first)),
            conjunction ? ShortCircuitOperator::And : ShortCircuitOperator::Or,
            UniqueIndirect(std::move(*second))
        },
        known,
        span,
        std::move(pending),
        completes
    );
}

auto BodyExprSite::external_unary(UnaryOperator operation, Value operand, Span span) noexcept
    -> ExpressionResult<Value> {
    return external_operation(
        CppUnaryOperation {.operation = operation},
        std::move(operand),
        std::nullopt,
        span
    );
}

auto BodyExprSite::external_cast(ConstructionTypeRef type, Value operand, Span span) noexcept
    -> ExpressionResult<Value> {
    return external_operation(
        CppConvertOperation {.explicit_cast = true},
        std::move(operand),
        type,
        span
    );
}

auto BodyExprSite::external_binary(
    BinaryOperator operation,
    Value left,
    Value right,
    Span span
) noexcept -> ExpressionResult<Value> {
    auto pending = take_pending_failures(left);
    append_pending_failures(pending, take_pending_failures(right));
    auto first = body.consume_value(left, span, AccessMode::Read);
    if (!first.has_value()) {
        return std::unexpected(first.error());
    }
    auto second = body.consume_value(right, span, AccessMode::Read);
    if (!second.has_value()) {
        return std::unexpected(second.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({AccessMode::Read, std::move(*first)});
    operands.push_back({AccessMode::Read, std::move(*second)});
    auto result =
        body.cpp_expression(CppBinaryOperation {.operation = operation}, std::move(operands), span);
    if (result.has_value()) {
        result->pending_failures = std::move(pending);
        result->completes = left.completes && right.completes;
    }
    return result;
}

auto BodyExprSite::extension(
    const ASTInterpolationExpr& source,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> ExpressionResult<Selection> {
    return construct_interpolation(*this, source, span);
}

auto BodyExprSite::extension(
    const ASTCppNameExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.select_cpp_name(value, span);
}

auto BodyExprSite::extension(
    const ASTNameExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.select_name(value, span);
}

auto BodyExprSite::extension(
    const ASTArrayExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return construct_array_expression(*this, value, span, expected);
}

auto BodyExprSite::extension(
    const ASTConstructionExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return construct_structure_expression(*this, value, span);
}

auto BodyExprSite::extension(
    const ASTAccessExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.access_expression(value, span);
}

auto BodyExprSite::extension(
    const ASTIndexExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return construct_index_expression(*this, value, span);
}

auto BodyExprSite::extension(
    const ASTPropagationExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.propagation_expression(value, span);
}

auto BodyExprSite::extension(
    const ASTIfForm& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.conditional_expression(value, span, expected, allow_pointer_narrowing);
}

auto BodyExprSite::extension(
    const ASTLambdaExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.lambda_expression(value, span, expected);
}

auto BodyExprSite::extension(
    const ASTMatchForm& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.match_expression(value, span, expected, allow_pointer_narrowing);
}

auto BodyExprSite::extension(
    const ASTTryForm& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Selection> {
    return this->body.try_expression(value, span, expected, allow_pointer_narrowing);
}

auto BodyExprSite::resolve_name(std::string_view name, Span span) noexcept
    -> ExpressionResult<std::optional<ConstantID>> {
    return body.resolve_constant_name(name, span);
}

auto BodyExprSite::resolve_enum_qualifier(ASTExprID id) noexcept
    -> ExpressionResult<std::optional<TypeID>> {
    return body.resolve_enum_qualifier(id);
}

auto BodyExprSite::resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
    -> ExpressionResult<ResolvedEnumCase> {
    return body.resolve_constant_enum_case(type, name, span);
}

auto BodyExprSite::is_numeric_enum(TypeID type) noexcept -> bool {
    return numeric_enum(type);
}

auto BodyExprSite::admits(const ASTExpr&) const noexcept -> bool {
    return true;
}

auto BodyExprSite::spelling(Span span) const noexcept -> std::string {
    return body.spelling(span);
}

auto BodyExprSite::invalid_enum_qualifier(Span span) noexcept -> ExpressionResult<Value> {
    return std::unexpected(fail(
        span,
        DiagnosticCode::TypeEnumContext,
        "enum case qualifier does not name an enum type"
    ));
}

auto BodyExprSite::convert_argument(Value& value, ConstructionTypeRef type, Span span) noexcept
    -> ExpressionResult<void> {
    const auto pending = take_pending_failures(value);
    auto converted = body.coerce_to(value, type, span);
    append_pending_failures(value.pending_failures, pending);
    return converted;
}

auto BodyExprSite::enum_constructor(
    TypeID enumeration_type,
    const ResolvedEnumCase& selected,
    Span span
) noexcept -> Value {
    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(selected.payload_types.size());
    for (const auto type : selected.payload_types) {
        parameters.push_back(
            ConstructionCallableParameter {
                .access = AccessMode::Read,
                .type = type,
            }
        );
    }
    const auto type = body.draft().append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = std::move(parameters),
                .result = enumeration_type,
                .failures = body.draft().add_empty_failure_term(),
            },
        }
    );
    auto value = body.active_builder().make_expression(
        type,
        body.active_builder().lifetime(),
        body.origin(span),
        SemEnumConstructor {.enum_case = selected.id}
    );
    return BuiltExpression {
        .storage = std::move(value),

        .pending_failures = {},
        .takeable = false,
    };
}

auto BodyExprSite::member_call(
    const ASTCallExpr& source,
    const ASTMemberExpr& member,
    Value operand,
    Span span
) noexcept -> ExpressionResult<Value> {
    auto callee = construct_member_expression(
        *this,
        member,
        std::move(operand),
        syntax().expression(source.callee).span
    );
    if (!callee.has_value()) {
        return std::unexpected(callee.error());
    }
    return body.call_expression(source, span, std::move(*callee));
}

auto BodyExprSite::call(const ASTCallExpr& source, Span span) noexcept -> ExpressionResult<Value> {
    return body.call_expression(source, span);
}

auto BodyExprSite::finish(
    ConstructionTypeRef type,
    SemanticExpressionValue value,
    std::optional<ConstantID> known,
    Span span,
    BodyPendingFailureTerms pending,
    bool completes
) noexcept -> Value {
    auto result = body.make_built(type, std::move(value), span, std::move(pending), known);
    result.completes = completes;
    return result;
}

auto BodyExprSite::external_operation(
    CppOperation operation,
    Value operand,
    std::optional<ConstructionTypeRef> type,
    Span span
) noexcept -> ExpressionResult<Value> {
    auto pending = take_pending_failures(operand);
    auto expression = body.consume_value(operand, span, AccessMode::Read);
    if (!expression.has_value()) {
        return std::unexpected(expression.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({AccessMode::Read, std::move(*expression)});
    auto result = body.cpp_expression(std::move(operation), std::move(operands), span, type);
    if (result.has_value()) {
        result->pending_failures = std::move(pending);
        result->completes = operand.completes;
    }
    return result;
}

auto BodyExprSite::module_id() const noexcept -> ProgramModuleID {
    return body.source_module_id;
}

auto BodyExprSite::permits_pointer_narrowing() const noexcept -> bool {
    return allow_pointer_narrowing;
}

auto BodyExprSite::infer_type(Value& value, Span span) noexcept
    -> ExpressionResult<ConstructionTypeRef> {
    return body.infer_value_type(value, span);
}

auto BodyExprSite::aggregate_cost(std::size_t, Span) const noexcept -> ExpressionResult<void> {
    return {};
}

auto BodyExprSite::aggregate_admitted(ConstructionTypeRef, Span) const noexcept
    -> ExpressionResult<bool> {
    return true;
}

auto BodyExprSite::read_argument(
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<Value> {
    return read_value_argument(*this, expression, expected);
}

auto BodyExprSite::consume_read(OperandState& state, Value value, Span span) noexcept
    -> ExpressionResult<SemanticExpression> {
    state.completes &= value.completes;
    append_pending_failures(state.pending, take_pending_failures(value));
    auto result = body.consume_value(value, span, AccessMode::Read);
    if (result) {
        result->constant = body.active_builder().known_constant(*result);
    }
    return result;
}

auto BodyExprSite::finish_constructed(
    ConstructionTypeRef type,
    SemanticExpressionValue value,
    OperandState state,
    Span span,
    std::optional<ConstantID> known
) noexcept -> Value {
    return finish(type, std::move(value), known, span, std::move(state.pending), state.completes);
}

auto BodyExprSite::cpp_construct(
    const ASTConstructionExpr& source,
    ConstructionTypeRef type,
    Span span
) noexcept -> ExpressionResult<Value> {
    return body.cpp_construct(source, type, span);
}

auto BodyExprSite::consume_write(OperandState& state, Value value, Span span) noexcept
    -> ExpressionResult<SemanticExpression> {
    state.completes &= value.completes;
    append_pending_failures(state.pending, take_pending_failures(value));
    auto place = body.consume_place(value, span);
    if (!place) {
        return std::unexpected(place.error());
    }
    return std::move(place->expression);
}

auto BodyExprSite::known_sequence_extent(const Value& value) const noexcept
    -> std::optional<std::uint64_t> {
    return body.active_builder().known_sequence_extent(value.expression());
}

auto BodyExprSite::require_invariant_storage(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    Span span
) noexcept -> ExpressionResult<void> {
    return body.require_invariant_type(source, target, span);
}

auto BodyExprSite::read_array_element(
    ASTExprID id,
    std::optional<ConstructionTypeRef> expected,
    bool explicit_context
) noexcept -> ExpressionResult<Value> {
    return body.expression(id, expected, explicit_context && allow_pointer_narrowing);
}
