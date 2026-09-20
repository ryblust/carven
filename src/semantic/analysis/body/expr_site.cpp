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
    -> ExpressionTask<std::optional<FunctionID>> {
    co_return (co_await body.resolve_function(name, span));
}

auto BodyExprSite::syntax() const noexcept -> ASTView {
    return body.ast;
}

auto BodyExprSite::fail(Span span, DiagnosticCode code, std::string message) noexcept
    -> AnalysisFailure {
    return body.fail(span, code, std::move(message));
}

auto BodyExprSite::read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
    -> ExpressionTask<Value> {
    co_return (co_await body.expression(id, expected, allow_pointer_narrowing));
}

auto BodyExprSite::type(const Value& value) const noexcept -> ConstructionTypeRef {
    return value.type();
}

auto BodyExprSite::known(const Value& value) const noexcept -> std::optional<ConstantID> {
    return value.constant();
}

auto BodyExprSite::condition_constant(const Value& value) const noexcept
    -> std::optional<ConstantID> {
    return body.active_builder().known_constant(value.expression());
}

auto BodyExprSite::external(ConstructionTypeRef type) const noexcept -> bool {
    return body.is_cpp_type(type);
}

auto BodyExprSite::dereference(const ASTPrefixExpr& source, Span span) noexcept
    -> ExpressionTask<Value> {
    co_return (co_await body.dereference_expression(source, span));
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
    -> ExpressionTask<ConstructionTypeRef> {
    co_return (co_await body.resolve_construction_type(type));
}

auto BodyExprSite::resolve_type(ASTTypeID type) noexcept -> ExpressionTask<ConstructionTypeRef> {
    co_return (co_await body.resolve_type(type));
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
    const auto truth = known_boolean_constant(draft(), condition_constant(left));
    const auto completes = left.completes && (truth == !conjunction || right.completes);
    auto first = body.consume_value(left, span, AccessMode::Read);
    if (!first.has_value()) {
        return std::unexpected(first.error());
    }
    auto second = body.consume_value(right, span, AccessMode::Read);
    if (!second.has_value()) {
        return std::unexpected(second.error());
    }
    first->constant = body.active_builder().known_constant(*first);
    second->constant = body.active_builder().known_constant(*second);
    return finish(
        draft().builtin_type(BuiltinType::Bool),
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
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await construct_interpolation(*this, source, span));
}

auto BodyExprSite::extension(
    const ASTCppNameExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return this->body.select_cpp_name(value, span);
}

auto BodyExprSite::extension(
    const ASTNameExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await this->body.select_name(value, span));
}

auto BodyExprSite::extension(
    const ASTArrayExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await construct_array_expression(*this, value, span, expected));
}

auto BodyExprSite::extension(
    const ASTConstructionExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await construct_structure_expression(*this, value, span, expected));
}

auto BodyExprSite::extension(
    const ASTAccessExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await this->body.access_expression(value, span));
}

auto BodyExprSite::extension(
    const ASTIndexExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await construct_index_expression(*this, value, span));
}

auto BodyExprSite::extension(
    const ASTPropagationExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await this->body.propagation_expression(value, span));
}

auto BodyExprSite::extension(
    const ASTIfForm& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (
        co_await this->body.conditional_expression(value, span, expected, allow_pointer_narrowing)
    );
}

auto BodyExprSite::extension(
    const ASTLambdaExpr& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await this->body.lambda_expression(value, span, expected));
}

auto BodyExprSite::extension(
    const ASTMatchForm& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (
        co_await this->body.match_expression(value, span, expected, allow_pointer_narrowing)
    );
}

auto BodyExprSite::extension(
    const ASTTryForm& value,
    Span span,
    [[maybe_unused]] std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<Selection> {
    co_return (co_await this->body.try_expression(value, span, expected, allow_pointer_narrowing));
}

auto BodyExprSite::resolve_name(std::string_view name, Span span) noexcept
    -> ExpressionTask<std::optional<ConstantID>> {
    co_return (co_await body.resolve_constant_name(name, span));
}

auto BodyExprSite::resolve_nominal_qualifier(ASTExprID id) noexcept
    -> ExpressionTask<std::optional<TypeID>> {
    co_return (co_await body.resolve_nominal_qualifier(id));
}

auto BodyExprSite::resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
    -> ExpressionTask<ResolvedEnumCase> {
    co_return (co_await body.resolve_constant_enum_case(type, name, span));
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

auto BodyExprSite::invalid_nominal_qualifier(Span span) noexcept -> ExpressionResult<Value> {
    return std::unexpected(fail(
        span,
        DiagnosticCode::TypeMismatch,
        "scope qualifier does not name an enum or class type"
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
        .completes = true,
    };
}

auto BodyExprSite::member_call(
    const ASTCallExpr& source,
    const ASTMemberExpr& member,
    Value operand,
    Span span
) noexcept -> ExpressionTask<Value> {
    const auto type = operand.type();
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft().type_copy(*concrete);
        if (const auto* record = std::get_if<StructTypeValue>(&canonical.value)) {
            const auto declaration =
                draft().construction_struct_declaration_copy(record->structure);
            const auto name = spelling(member.name_span);
            const auto field =
                std::ranges::find_if(declaration.fields, [&](const auto& value) noexcept {
                    return draft().spelling(value.name) == name;
                });
            if (declaration.kind == RecordKind::Class && field == declaration.fields.end()) {
                co_return (
                    co_await body
                        .class_call(source, member, record->structure, std::move(operand), span)
                );
            }
        }
    }
    auto callee = construct_member_expression(
        *this,
        member,
        std::move(operand),
        syntax().expression(source.callee).span
    );
    if (!callee.has_value()) {
        co_return std::unexpected(callee.error());
    }
    co_return (co_await body.call_expression(source, span, std::move(*callee)));
}

auto BodyExprSite::call(const ASTCallExpr& source, Span span) noexcept -> ExpressionTask<Value> {
    co_return (co_await body.call_expression(source, span));
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
) noexcept -> ExpressionTask<Value> {
    co_return (co_await read_value_argument(*this, expression, expected));
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
) noexcept -> ExpressionTask<Value> {
    co_return (co_await body.cpp_construct(source, type, span));
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
) noexcept -> ExpressionTask<Value> {
    co_return (co_await body.expression(id, expected, explicit_context && allow_pointer_narrowing));
}

auto BodyExprSite::operand_state() noexcept -> OperandState {
    return {.pending = {}, .completes = true};
}

auto BodyExprSite::representation_access(StructID owner, Span span) noexcept
    -> AnalysisResult<void> {
    if (body.draft().construction_struct_declaration_copy(owner).kind == RecordKind::Class
        && body.lexical_class != owner) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::AccessClassPrivate,
            "class representation is accessible only inside its defining class"
        ));
    }
    return {};
}

auto BodyExprSite::associated_call(
    const ASTCallExpr& source,
    const ASTMemberExpr& member,
    StructID owner,
    Span span
) noexcept -> ExpressionTask<Value> {
    co_return (co_await body.class_call(source, member, owner, std::nullopt, span));
}

auto BodyExprSite::associated_reference(StructID owner, Span name_span) noexcept
    -> ExpressionTask<Value> {
    co_return (co_await body.class_operation(owner, spelling(name_span), false, name_span));
}
