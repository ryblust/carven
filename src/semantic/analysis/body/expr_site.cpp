module carven:semantic.analysis.body.expr_site.impl;

import :semantic.analysis.body.context;
import :semantic.analysis.body.expr_site;
import :semantic.analysis.constant.fold;
import :semantic.analysis.construction.limits;
import :semantic.analysis.construction.requests;
import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.interpolation;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.projection;
import :semantic.analysis.expr.scope;
import :semantic.analysis.expr.text;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :support.invariant;
import :support.unique_indirect;
import std;

BodyExprSite::BodyExprSite(BodyElaborator& body, bool allow_pointer_narrowing) noexcept
    : body(body),
      allow_pointer_narrowing(allow_pointer_narrowing) {}

auto BodyExprSite::draft() noexcept -> ProgramDraft& {
    return body.draft();
}

auto BodyExprSite::catalog() const noexcept -> AnalysisCatalogView {
    return body.catalog();
}

auto BodyExprSite::construction_requests() noexcept -> ConstructionRequests& {
    return body.construction_requests();
}

auto BodyExprSite::syntax() const noexcept -> ASTView {
    return body.ast;
}

auto BodyExprSite::observe_expression(ASTExprID expression, ConstructionTypeRef type) noexcept
    -> void {
    body.observe_expression(expression, type);
}

auto BodyExprSite::fail(Span span, DiagnosticCode code, std::string message) noexcept
    -> AnalysisFailure {
    return body.fail(span, code, std::move(message));
}

auto BodyExprSite::read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
    -> AnalysisTask<Value> {
    co_return (co_await body.expression(id, expected, allow_pointer_narrowing));
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
    -> AnalysisTask<Value> {
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
    -> AnalysisTask<ConstructionTypeRef> {
    co_return (co_await body.resolve_construction_type(type));
}

auto BodyExprSite::resolve_type(ASTTypeID type) noexcept -> AnalysisTask<ConstructionTypeRef> {
    co_return (co_await body.resolve_type(type));
}

auto BodyExprSite::resolve_duration_type(Span span) noexcept -> AnalysisTask<TypeID> {
    co_return (co_await body.resolve_duration_type(span));
}

auto BodyExprSite::constant(ConstantID constant, Span span) noexcept -> Value {
    return body
        .make_built(draft().constant(constant).type, constant_operation(draft(), constant), span);
}

auto BodyExprSite::resolve_generic_arguments(
    std::span<const ASTGenericArgument> arguments,
    std::span<const GenericParameterKind> policies
) noexcept -> AnalysisTask<GenericEnvironment> {
    const auto extent = [&](ASTExprID expression) noexcept {
        return body.resolve_array_extent(expression);
    };
    co_return co_await resolve_generic_source_arguments(
        draft(),
        body.catalog(),
        body.import_usage(),
        body.source_module_id,
        body.ast,
        arguments,
        policies,
        extent,
        &body.batch->requests,
        body.generic_context ? &*body.generic_context : nullptr,
        body.observe_sources ? &body.source_occurrences : nullptr
    );
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
) noexcept -> AnalysisResult<Value> {
    auto pending = take_pending_failures(left);
    append_pending_failures(pending, take_pending_failures(right));
    const auto completes = left.completes;
    auto first = body.consume_value(left, span, AccessMode::Read);
    if (!first.has_value()) {
        return std::unexpected(first.error());
    }
    auto second = body.consume_value(right, span, AccessMode::Read);
    if (!second.has_value()) {
        return std::unexpected(second.error());
    }
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
    -> AnalysisResult<Value> {
    return external_operation(
        CppUnaryOperation {.operation = operation},
        std::move(operand),
        std::nullopt,
        span
    );
}

auto BodyExprSite::external_cast(ConstructionTypeRef type, Value operand, Span span) noexcept
    -> AnalysisResult<Value> {
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
) noexcept -> AnalysisResult<Value> {
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
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_interpolation(*this, source, span));
}

auto BodyExprSite::extension(
    const ASTCppNameExpr& value,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return this->body.select_cpp_name(value, span);
}

auto BodyExprSite::extension(
    const ASTNameExpr& value,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await this->body.select_name(value, span));
}

auto BodyExprSite::extension(
    const ASTTupleExpr& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_tuple_expression(*this, value, span, expected));
}

auto BodyExprSite::extension(
    const ASTTupleElementExpr& value,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_tuple_element_expression(*this, value, span));
}

auto BodyExprSite::extension(
    const ASTTypeApplicationExpr&,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return std::unexpected(fail(
        span,
        DiagnosticCode::TypeGenericArguments,
        "type arguments require a generic function or a nominal type qualifier"
    ));
}

auto BodyExprSite::extension(
    const ASTArrayExpr& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_array_expression(*this, value, span, expected));
}

auto BodyExprSite::extension(
    const ASTArrayRepeatExpr& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_array_expression(*this, value, span, expected));
}

auto BodyExprSite::extension(
    const ASTConstructionExpr& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_structure_expression(*this, value, span, expected));
}

auto BodyExprSite::extension(
    const ASTAccessExpr& value,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await this->body.access_expression(value, span));
}

auto BodyExprSite::extension(
    const ASTIndexExpr& value,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await construct_index_expression(*this, value, span));
}

auto BodyExprSite::extension(
    const ASTPropagationExpr& value,
    Span span,
    std::optional<ConstructionTypeRef>
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await this->body.propagation_expression(value, span));
}

auto BodyExprSite::extension(
    const ASTIfForm& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (
        co_await this->body.conditional_expression(value, span, expected, allow_pointer_narrowing)
    );
}

auto BodyExprSite::extension(
    const ASTLambdaExpr& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await this->body.lambda_expression(value, span, expected));
}

auto BodyExprSite::extension(
    const ASTMatchForm& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (
        co_await this->body.match_expression(value, span, expected, allow_pointer_narrowing)
    );
}

auto BodyExprSite::extension(
    const ASTTryForm& value,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Selection> {
    co_return (co_await this->body.try_expression(value, span, expected, allow_pointer_narrowing));
}

auto BodyExprSite::resolve_type_qualifier(ASTExprID id) noexcept
    -> AnalysisTask<std::optional<ConstructionTypeRef>> {
    co_return (co_await body.resolve_type_qualifier(id));
}

auto BodyExprSite::resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
    -> AnalysisTask<ResolvedEnumCase> {
    co_return (co_await body.resolve_constant_enum_case(type, name, span));
}

auto BodyExprSite::spelling(Span span) const noexcept -> std::string {
    return body.spelling(span);
}

auto BodyExprSite::invalid_type_qualifier(Span span) noexcept -> AnalysisResult<Value> {
    return std::unexpected(
        fail(span, DiagnosticCode::TypeMismatch, "scope qualifier does not name a type")
    );
}

auto BodyExprSite::convert_argument(Value& value, ConstructionTypeRef type, Span span) noexcept
    -> AnalysisResult<void> {
    const auto pending = take_pending_failures(value);
    auto converted = body.coerce_to(value, type, span);
    append_pending_failures(value.pending_failures, pending);
    return converted;
}

auto BodyExprSite::enum_constructor(const ResolvedEnumCase& selected, Span span) noexcept -> Value {
    auto value = body.active_builder().make_expression(
        selected.reference_type,
        body.active_builder().lifetime(),
        body.origin(span),
        SemEnumConstructor {.enum_case = selected.id}
    );
    return BuiltExpression {
        .storage = UniqueIndirect {BodyExpressionStorage {std::move(value)}},
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
) noexcept -> AnalysisTask<Value> {
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

auto BodyExprSite::call(const ASTCallExpr& source, Span span) noexcept -> AnalysisTask<Value> {
    co_return (co_await body.call_expression(source, span));
}

auto BodyExprSite::finish(
    ConstructionTypeRef type,
    SemanticExpressionValue&& value,
    std::optional<ConstantID> known,
    Span span,
    BodyPendingFailures pending,
    bool completes
) noexcept -> AnalysisResult<Value> {
    auto result = body.make_built(type, std::move(value), span, std::move(pending), known);
    result.completes = completes;
    if (!body.static_stage()) {
        auto* expression = std::get_if<SemanticExpression>(&*result.storage);
        if (!expression) {
            invariant_violation("constructed expression is not a value");
        }
        auto folded = fold_constant_expression(draft(), *expression);
        if (!folded) {
            return std::unexpected(folded.error());
        }
    }
    return result;
}

auto BodyExprSite::external_operation(
    CppOperation operation,
    Value operand,
    std::optional<ConstructionTypeRef> type,
    Span span
) noexcept -> AnalysisResult<Value> {
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
    -> AnalysisResult<ConstructionTypeRef> {
    return body.infer_value_type(value, span);
}

auto BodyExprSite::aggregate_cost(std::size_t count, Span span) noexcept -> AnalysisResult<void> {
    if (count > maximum_construction_work - body.aggregate_construction_work) {
        return std::unexpected(
            fail(span, DiagnosticCode::ConstLimit, "aggregate construction exceeds its work budget")
        );
    }
    body.aggregate_construction_work += count;
    return {};
}

auto BodyExprSite::resolve_array_extent(ASTExprID expression) noexcept
    -> AnalysisTask<std::uint64_t> {
    co_return (co_await body.resolve_array_extent(expression));
}

auto BodyExprSite::consume_unexecuted_read(Value value, Span span) noexcept
    -> AnalysisResult<SemanticExpression> {
    return body.consume_value(value, span, AccessMode::Read);
}

auto BodyExprSite::read_argument(
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Value> {
    co_return (co_await read_value_argument(*this, expression, expected));
}

auto BodyExprSite::read_static_argument(
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<Value> {
    const auto selected = call_argument_operand(syntax(), expression);
    const auto span = syntax().expression(expression).span;
    if (selected.access != AccessMode::Read) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::AccessCallMismatch,
            "argument access marker differs from the parameter"
        ));
    }
    auto built = co_await body.static_expression(selected.expression, expected);
    if (!built) {
        co_return std::unexpected(built.error());
    }
    if (built->type() == ConstructionTypeRef(draft().builtin_type(BuiltinType::Void))) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::TypeValueRequired, "expression does not produce a value")
        );
    }
    co_return std::move(*built);
}

auto BodyExprSite::consume_read(OperandState& state, Value value, Span span) noexcept
    -> AnalysisResult<SemanticExpression> {
    state.completes &= value.completes;
    append_pending_failures(state.pending, take_pending_failures(value));
    auto result = body.consume_value(value, span, AccessMode::Read);
    return result;
}

auto BodyExprSite::consume_take(OperandState& state, Value value, Span span) noexcept
    -> AnalysisResult<SemanticExpression> {
    state.completes &= value.completes;
    append_pending_failures(state.pending, take_pending_failures(value));
    return body.consume_value(value, span, AccessMode::Take);
}

auto BodyExprSite::consume_cold_owner(
    OperandState& state,
    Value value,
    AccessMode access,
    Span span
) noexcept -> AnalysisResult<SemanticExpression> {
    const auto* binding = std::get_if<SemBinding>(&value.expression().value);
    if ((binding && body.async_child_bindings.contains(binding->binding))
        || access == AccessMode::Write
        || (std::holds_alternative<PlaceExpression>(*value.storage)
            && access != AccessMode::Take)) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::AsyncOwnership,
            "composition takes cold owners; named operations require an explicit Take argument"
        ));
    }
    return consume_take(state, std::move(value), span);
}

auto BodyExprSite::require_matching_type(
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    Span span
) noexcept -> AnalysisResult<void> {
    return body.require_invariant_type(source, target, span);
}

auto BodyExprSite::consume_read_argument(OperandState& state, Value value, Span span) noexcept
    -> AnalysisResult<SemanticExpression> {
    auto argument = body.bind_call_argument(
        std::move(value),
        AccessMode::Read,
        std::nullopt,
        span,
        std::nullopt
    );
    if (!argument) {
        return std::unexpected(argument.error());
    }
    state.completes &= argument->completes;
    append_pending_failures(state.pending, argument->pending_failures);
    return std::move(argument->argument.expression);
}

auto BodyExprSite::finish_constructed(
    ConstructionTypeRef type,
    SemanticExpressionValue value,
    OperandState state,
    Span span,
    std::optional<ConstantID> known
) noexcept -> AnalysisResult<Value> {
    return finish(type, std::move(value), known, span, std::move(state.pending), state.completes);
}

auto BodyExprSite::cpp_construct(
    const ASTConstructionExpr& source,
    ConstructionTypeRef type,
    Span span
) noexcept -> AnalysisTask<Value> {
    co_return (co_await body.cpp_construct(source, type, span));
}

auto BodyExprSite::consume_write(OperandState& state, Value value, Span span) noexcept
    -> AnalysisResult<SemanticExpression> {
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
) noexcept -> AnalysisResult<void> {
    return body.require_invariant_type(source, target, span);
}

auto BodyExprSite::read_aggregate_element(
    ASTExprID id,
    std::optional<ConstructionTypeRef> expected,
    bool explicit_context
) noexcept -> AnalysisTask<Value> {
    co_return (co_await body.expression(id, expected, explicit_context && allow_pointer_narrowing));
}

auto BodyExprSite::operand_state() noexcept -> OperandState {
    return {.pending = {}, .completes = true};
}

auto BodyExprSite::representation_access(StructID owner, Span span) noexcept
    -> AnalysisResult<void> {
    return check_class_representation_access(
        draft(),
        body.source_module_id,
        owner,
        body.lexical_class,
        span
    );
}

auto BodyExprSite::associated_call(
    const ASTCallExpr& source,
    const ASTMemberExpr& member,
    StructID owner,
    Span span
) noexcept -> AnalysisTask<Value> {
    co_return (co_await body.class_call(source, member, owner, std::nullopt, span));
}

auto BodyExprSite::associated_reference(StructID owner, Span name_span) noexcept
    -> AnalysisTask<Value> {
    co_return (co_await body.class_operation(owner, spelling(name_span), false, name_span));
}

auto BodyExprSite::standard_call(
    const ASTCallExpr& source,
    StandardDeclaration function,
    Span span
) noexcept -> AnalysisTask<Value> {
    auto function_id =
        co_await construction_requests().standard_function(function, module_id(), span);
    if (!function_id) {
        co_return std::unexpected(function_id.error());
    }
    auto ready =
        co_await construction_requests().ensure_function_signature(*function_id, module_id(), span);
    if (!ready) {
        co_return std::unexpected(ready.error());
    }
    auto callee_id = source.callee;
    while (const auto* group = std::get_if<ASTGroupExpr>(&syntax().expression(callee_id).value)) {
        callee_id = group->expression;
    }
    observe_function(
        *function_id,
        std::get<ASTMemberExpr>(syntax().expression(callee_id).value).name_span
    );
    const auto target = draft().function_declaration_copy(*function_id).callable;
    auto callee = body.make_built(
        draft().intern_type({.value = FunctionTypeValue {target}}),
        SemCallable {.callable = target},
        span
    );
    co_return co_await body.call_expression(source, span, std::move(callee));
}

auto BodyExprSite::number_parse_call(
    const ASTCallExpr& source,
    BuiltinType number,
    Span span
) noexcept -> AnalysisTask<Value> {
    auto target = co_await resolve_number_parser(number, span);
    if (!target) {
        co_return std::unexpected(target.error());
    }
    auto callee = body.make_built(
        draft().intern_type({.value = FunctionTypeValue {*target}}),
        SemCallable {.callable = *target},
        span
    );
    co_return co_await body.call_expression(source, span, std::move(callee));
}

auto BodyExprSite::resolve_number_parser(BuiltinType number, Span span) noexcept
    -> AnalysisTask<CallableID> {
    co_return co_await body.resolve_number_parser(number, span);
}

auto BodyExprSite::native_associated_reference(
    ConstructionTypeRef type,
    Span name_span,
    Span span
) noexcept -> Selection {
    return CppSelection {
        .target =
            CppStaticMemberCallee<ConstructionTypeRef> {
                .type = type,
                .member = spelling(name_span)
            },
        .span = span
    };
}

// Initial source operations retain exact delayed failure terms until canonicalization.
auto BodyElaborator::operation_shape(ConstructionTypeRef type) const noexcept
    -> std::optional<ConstructionOperationTypeValue> {
    return ::operation_shape(draft(), type);
}

auto BodyElaborator::await_child_intrinsic(
    const ASTCallExpr& source,
    AsyncIntrinsic kind,
    Span span
) noexcept -> AnalysisTask<BuiltExpression> {
    const auto closing = kind == AsyncIntrinsic::CloseChild;
    if ((closing && source.arguments.size() != 1uz) || (!closing && source.arguments.empty())) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::TypeCallArity,
            closing ? "close requires exactly one lexical child"
                    : "wait_ready requires at least one lexical child"
        ));
    }
    auto children = std::vector<LocalBindingID>();
    for (const auto& argument : source.arguments) {
        const auto& argument_source = ast.expression(argument);
        if (!std::holds_alternative<ASTNameExpr>(argument_source.value)) {
            co_return std::unexpected(fail(
                argument_source.span,
                DiagnosticCode::AsyncOwnership,
                "child waiting and closing require named lexical children"
            ));
        }
        auto selected = co_await expression(argument);
        if (!selected) {
            co_return std::unexpected(selected.error());
        }
        const auto* binding = std::get_if<SemBinding>(&selected->expression().value);
        if (binding == nullptr
            || binding->binding.owner() != active_builder().identity()
            || !async_child_bindings.contains(binding->binding)) {
            co_return std::unexpected(fail(
                argument_source.span,
                DiagnosticCode::AsyncOwnership,
                "child waiting and closing require a lexical child in the current activation"
            ));
        }
        if (std::ranges::contains(children, binding->binding)) {
            co_return std::unexpected(fail(
                argument_source.span,
                DiagnosticCode::AsyncOwnership,
                "wait_ready cannot select the same lexical child twice"
            ));
        }
        children.push_back(binding->binding);
    }
    if (closing) {
        co_return make_built(
            ConstructionTypeRef(draft().builtin_type(BuiltinType::Void)),
            SemCloseChild {.child = children.front()},
            span
        );
    }
    const auto type = draft().intern_type(
        CanonicalType {
            .value = TupleTypeValue {
                .elements = {
                    draft().builtin_type(BuiltinType::Usize),
                    draft().builtin_type(BuiltinType::Bool)
                }
            }
        }
    );
    co_return make_built(
        ConstructionTypeRef(type),
        SemWaitReady {.children = std::move(children)},
        span
    );
}

auto BodyElaborator::await_expression(const ASTPrefixExpr& source, Span span) noexcept
    -> AnalysisTask<BuiltExpression> {
    if (!async_body && !static_stage()) {
        co_return std::unexpected(fail(
            source.operator_span,
            DiagnosticCode::AsyncAdmission,
            "await requires an async fn body or const execution"
        ));
    }
    auto operand_id = source.operand_id;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(operand_id).value)) {
        operand_id = group->expression;
    }
    if (const auto* call = std::get_if<ASTCallExpr>(&ast.expression(operand_id).value)) {
        auto callee_id = call->callee;
        while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(callee_id).value)) {
            callee_id = group->expression;
        }
        if (const auto* name = std::get_if<ASTNameExpr>(&ast.expression(callee_id).value)) {
            const auto text = spelling(name->name_span);
            const auto candidates = catalog().lookup(source_module_id, text);
            const auto* provided = candidates.size() == 1uz
                ? std::get_if<ProvidedExport>(&candidates.front().entity)
                : nullptr;
            if (find_local(text) == nullptr
                && provided
                && (*provided == ProvidedExport::WaitReady || *provided == ProvidedExport::Close)) {
                auto callee = co_await select_expression(call->callee);
                if (!callee) {
                    co_return std::unexpected(callee.error());
                }
                const auto* intrinsic = std::get_if<AsyncIntrinsicSelection>(&*callee);
                if (intrinsic == nullptr) {
                    invariant_violation("selected child observer lost its intrinsic identity");
                }
                co_return co_await await_child_intrinsic(*call, intrinsic->kind, span);
            }
        }
    }
    auto operand = co_await expression(source.operand_id);
    if (!operand) {
        co_return std::unexpected(operand.error());
    }
    const auto operation = operation_shape(operand->type());
    if (!operation) {
        co_return std::unexpected(fail(
            span,
            DiagnosticCode::AsyncAdmission,
            "await requires a complete cold operation owner or lexical async child"
        ));
    }
    const auto* binding = std::get_if<SemBinding>(&operand->expression().value);
    const auto child = binding && async_child_bindings.contains(binding->binding);
    auto pending = take_pending_failures(*operand);
    auto value = child
        ? consume_place(*operand, span).transform([](PlaceExpression&& place) static noexcept {
              return std::move(place.expression);
          })
        : consume_value(*operand, span, AccessMode::Take);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    pending.push_back(operation->failures);
    auto result = make_built(
        operation->success,
        SemAwait {
            .operand = UniqueIndirect(std::move(*value)),
            .operand_kind =
                child ? AsyncAwaitOperandKind::LexicalChild : AsyncAwaitOperandKind::ColdOperation
        },
        span,
        std::move(pending)
    );
    result.completes = operand->completes;
    co_return result;
}

auto BodyExprSite::await_expression(const ASTPrefixExpr& source, Span span) noexcept
    -> AnalysisTask<Value> {
    co_return co_await body.await_expression(source, span);
}

auto BodyExprSite::observe_function(FunctionID function, Span name) noexcept -> void {
    const auto callable = draft().function_declaration_copy(function).callable;
    body.observe_source(
        name,
        body.catalog().declaration_location(draft(), body.catalog().function_symbol(function)),
        draft().intern_type({.value = FunctionTypeValue {callable}})
    );
}
