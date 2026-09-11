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

    explicit BodyExpressionSite(BodyElaborator& body, bool allow_pointer_narrowing = true) noexcept
        : body(body),
          allow_pointer_narrowing(allow_pointer_narrowing) {}

    auto draft() noexcept -> ProgramDraft& { return body.draft(); }

    auto syntax() const noexcept -> ASTView { return body.ast; }

    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure {
        return body.fail(span, code, std::move(message));
    }

    auto read(ASTExprID id, std::optional<ConstructionTypeRef> expected) noexcept
        -> AnalysisResult<Value> {
        return body.expression(id, expected, allow_pointer_narrowing);
    }

    auto present(const Value&) const noexcept -> bool { return true; }

    auto unavailable() const noexcept -> Value {
        invariant_violation("body expression became unavailable");
    }

    auto type(const Value& value) const noexcept -> ConstructionTypeRef { return value.type(); }

    auto known(const Value& value) const noexcept -> std::optional<ConstantID> {
        return value.constant();
    }

    auto external(ConstructionTypeRef type) const noexcept -> bool {
        return body.is_cpp_type(type);
    }

    auto dereference(const ASTPrefixExpr& source, Span span) noexcept -> AnalysisResult<Value> {
        return body.dereference_expression(source, span);
    }

    auto supports_equality(ConstructionTypeRef type) noexcept -> bool {
        return type_supports_equality(draft(), type);
    }

    auto numeric_enum(ConstructionTypeRef type) noexcept -> bool {
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

    auto resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef> {
        return body.resolve_type(type);
    }

    auto c_string(std::string_view bytes, Span span) noexcept -> Value {
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

    auto constant(ConstantID constant, Span span) noexcept -> Value {
        return body.make_built(
            draft().constant_copy(constant).type,
            SemConstant {.constant = constant},
            span
        );
    }

    auto enter_operand_execution(bool executed) noexcept -> BodyReferencePathGuard {
        return BodyReferencePathGuard(body.reference_path_reachable, executed);
    }

    auto finish_unary(
        UnaryOperator operation,
        ConstructionTypeRef type,
        Value operand,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto pending = take_pending_failures(operand);
        auto expression = body.consume_value(operand, span, AccessMode::Read);
        if (!expression.has_value()) {
            return std::unexpected(expression.error());
        }
        return finish(
            type,
            SemUnary {operation, UniqueIndirect(std::move(*expression))},
            known,
            span,
            std::move(pending),
            operand.completes
        );
    }

    auto finish_binary(
        BinaryOperator operation,
        ConstructionTypeRef type,
        Value left,
        Value right,
        std::optional<ConstantID> known,
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
        return finish(
            type,
            SemBinary {
                UniqueIndirect(std::move(*first)),
                operation,
                UniqueIndirect(std::move(*second))
            },
            known,
            span,
            std::move(pending),
            left.completes && right.completes
        );
    }

    auto finish_cast(
        CastKind kind,
        ConstructionTypeRef target,
        Value operand,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto pending = take_pending_failures(operand);
        auto expression = body.consume_value(operand, span, AccessMode::Read);
        if (!expression.has_value()) {
            return std::unexpected(expression.error());
        }
        return finish(
            target,
            SemCast {UniqueIndirect(std::move(*expression)), kind},
            known,
            span,
            std::move(pending),
            operand.completes
        );
    }

    auto finish_short_circuit(
        bool conjunction,
        Value left,
        Value right,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto pending = take_pending_failures(left);
        append_pending_failures(pending, take_pending_failures(right));
        const auto truth = known_boolean_constant(draft(), left.constant());
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

    auto external_unary(UnaryOperator operation, Value operand, Span span) noexcept
        -> AnalysisResult<Value> {
        return external_operation(
            CppUnaryOperation {.operation = operation},
            std::move(operand),
            std::nullopt,
            span
        );
    }

    auto external_cast(ConstructionTypeRef type, Value operand, Span span) noexcept
        -> AnalysisResult<Value> {
        return external_operation(
            CppConvertOperation {.explicit_cast = true},
            std::move(operand),
            type,
            span
        );
    }

    auto external_binary(BinaryOperator operation, Value left, Value right, Span span) noexcept
        -> AnalysisResult<Value> {
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
        auto result = body.cpp_expression(
            CppBinaryOperation {.operation = operation},
            std::move(operands),
            span
        );
        if (result.has_value()) {
            result->pending_failures = std::move(pending);
            result->completes = left.completes && right.completes;
        }
        return result;
    }

    auto extension(
        const ASTCppNameExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.select_cpp_name(value, span);
    }

    auto extension(
        const ASTNameExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.select_name(value, span);
    }

    auto extension(
        const ASTArrayExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.array_expression(value, span, expected, allow_pointer_narrowing);
    }

    auto extension(
        const ASTConstructionExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.construction_expression(value, span);
    }

    auto extension(
        const ASTAccessExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.access_expression(value, span);
    }

    auto extension(
        const ASTIndexExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.index_expression(value, span);
    }

    auto extension(
        const ASTPropagationExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.propagation_expression(value, span);
    }

    auto extension(
        const ASTIfForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.conditional_expression(value, span, expected, allow_pointer_narrowing);
    }

    auto extension(
        const ASTLambdaExpr& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.lambda_expression(value, span, expected);
    }

    auto extension(
        const ASTMatchForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.match_expression(value, span, expected, allow_pointer_narrowing);
    }

    auto extension(
        const ASTTryForm& value,
        Span span,
        [[maybe_unused]] std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<Result> {
        return this->body.try_expression(value, span, expected, allow_pointer_narrowing);
    }

    auto resolve_name(std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedConstantName> {
        return body.resolve_constant_name(name, span);
    }

    auto resolve_enum_qualifier(ASTExprID id) noexcept -> AnalysisResult<std::optional<TypeID>> {
        return body.resolve_enum_qualifier(id);
    }

    auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedEnumCase> {
        return body.resolve_constant_enum_case(type, name, span);
    }

    auto is_numeric_enum(TypeID type) noexcept -> bool { return numeric_enum(type); }

    auto admits(const ASTExpr&) const noexcept -> bool { return true; }

    auto spelling(Span span) const noexcept -> std::string { return body.spelling(span); }

    auto invalid_enum_qualifier(Span span) noexcept -> AnalysisResult<Value> {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "enum case qualifier does not name an enum type"
        ));
    }

    auto convert_argument(Value& value, ConstructionTypeRef type, Span span) noexcept
        -> AnalysisResult<void> {
        const auto pending = take_pending_failures(value);
        auto converted = body.coerce_to(value, type, span);
        append_pending_failures(value.pending_failures, pending);
        return converted;
    }

    auto enum_constructor(
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

    auto finish_enum_case(
        TypeID type,
        EnumCaseID selected,
        std::vector<Value> arguments,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto pending = BodyPendingFailureTerms();
        auto payload = std::vector<SemanticExpression>();
        auto completes = true;
        for (auto& argument : arguments) {
            append_pending_failures(pending, take_pending_failures(argument));
            completes &= argument.completes;
            auto value = body.consume_value(argument, span, AccessMode::Read);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            payload.push_back(std::move(*value));
        }
        return finish(
            type,
            SemEnumCase {.enum_case = selected, .payload = std::move(payload)},
            known,
            span,
            std::move(pending),
            completes
        );
    }

    auto extension(
        const ASTInterpolationExpr& source,
        Span span,
        std::optional<ConstructionTypeRef>
    ) noexcept -> AnalysisResult<Result> {
        auto format_string = std::string();
        auto operands = std::vector<SemCallArgument>();
        auto pending = BodyPendingFailureTerms();
        auto completes = true;
        const auto append = [&](this const auto& self,
                                const std::vector<ASTInterpolationPart>& parts,
                                bool in_specification) noexcept -> AnalysisResult<void> {
            for (const auto& part : parts) {
                if (const auto* text = std::get_if<ASTInterpolationText>(&part.value)) {
                    for (const auto byte : text->bytes) {
                        format_string.push_back(byte);
                        if (!in_specification && (byte == '{' || byte == '}')) {
                            format_string.push_back(byte);
                        }
                    }
                    continue;
                }
                const auto* hole = std::get_if<ASTInterpolationHole>(&part.value);
                const auto execution = enter_operand_execution(completes);
                auto built =
                    body.build_call_argument(hole->expression, AccessMode::Read, std::nullopt);
                if (!built) {
                    return std::unexpected(built.error());
                }
                append_pending_failures(pending, built->pending_failures);
                completes &= built->completes;
                format_string += std::format("{{{}", operands.size());
                operands.push_back(std::move(built->argument));
                if (hole->colon_span) {
                    format_string.push_back(':');
                    if (auto result = self(hole->specification, true); !result) {
                        return result;
                    }
                }
                format_string.push_back('}');
            }
            return {};
        };
        if (auto result = append(source.parts, false); !result) {
            return std::unexpected(result.error());
        }
        const auto constant_id = draft().intern_constant({
            .type = draft().intern_builtin_type(BuiltinType::Str),
            .value = StringConstant {.value = draft().intern_spelling(format_string)},
        });
        return finish(
            draft().intern_builtin_type(BuiltinType::String),
            SemFormat {.format_string_id = constant_id, .operands = std::move(operands)},
            std::nullopt,
            span,
            std::move(pending),
            completes
        );
    }

    auto finish_text(
        TextIntrinsic intrinsic,
        TypeID type,
        Value operand,
        std::optional<ConstantID> known,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto pending = take_pending_failures(operand);
        auto value = body.consume_value(operand, span, AccessMode::Read);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        auto operands = std::vector<SemCallArgument>();
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
        return finish(
            type,
            SemTextIntrinsic {.intrinsic = intrinsic, .operands = std::move(operands)},
            known,
            span,
            std::move(pending),
            operand.completes
        );
    }

    auto finish_slice_call(
        SliceIntrinsic intrinsic,
        Value receiver,
        std::span<const ASTCallArgument> arguments,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        const auto receiver_type = receiver.type();
        auto result_type = receiver_type;
        if (intrinsic == SliceIntrinsic::FromArray) {
            if (const auto* id = std::get_if<TypeID>(&receiver_type)) {
                const auto array = std::get<ArrayTypeValue>(draft().type_copy(*id).value);
                result_type =
                    draft().intern_type({.value = SliceTypeValue {.element = array.element}});
            } else {
                const auto array = std::get<ConstructionArrayTypeValue>(
                    draft().construction_type_copy(std::get<TypeTermID>(receiver_type)).value
                );
                result_type = draft().append_construction_type(
                    {.value = ConstructionSliceTypeValue {.element = array.element}}
                );
            }
        } else if (intrinsic == SliceIntrinsic::Len || intrinsic == SliceIntrinsic::IsEmpty) {
            result_type = draft().intern_builtin_type(
                intrinsic == SliceIntrinsic::Len ? BuiltinType::Usize : BuiltinType::Bool
            );
        }
        auto pending = take_pending_failures(receiver);
        auto completes = receiver.completes;
        auto value = body.consume_value(receiver, span, AccessMode::Read);
        if (!value) {
            return std::unexpected(value.error());
        }
        auto operands = std::vector<SemCallArgument>();
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
        for (const auto& argument : arguments) {
            auto built = body.build_call_argument(
                argument.expression,
                AccessMode::Read,
                draft().intern_builtin_type(BuiltinType::Usize)
            );
            if (!built) {
                return std::unexpected(built.error());
            }
            append_pending_failures(pending, built->pending_failures);
            completes &= built->completes;
            operands.push_back(std::move(built->argument));
        }
        return finish(
            result_type,
            SemSliceIntrinsic {.intrinsic = intrinsic, .operands = std::move(operands)},
            std::nullopt,
            span,
            std::move(pending),
            completes
        );
    }

    auto finish_text_call(
        TextIntrinsic intrinsic,
        std::optional<Value> receiver,
        std::span<const ASTCallArgument> arguments,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto pending = BodyPendingFailureTerms();
        auto operands = std::vector<SemCallArgument>();
        auto completes = true;
        if (receiver) {
            append_pending_failures(pending, take_pending_failures(*receiver));
            completes &= receiver->completes;
            if (text_intrinsic_writes(intrinsic)) {
                auto place = body.consume_place(*receiver, span);
                if (!place) {
                    return std::unexpected(place.error());
                }
                operands.push_back(
                    {.access = AccessMode::Write, .expression = std::move(place->expression)}
                );
            } else {
                auto value = body.consume_value(*receiver, span, AccessMode::Read);
                if (!value) {
                    return std::unexpected(value.error());
                }
                operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
            }
        }
        for (const auto& argument : arguments) {
            auto built = body.build_call_argument(
                argument.expression,
                AccessMode::Read,
                draft().intern_builtin_type(
                    intrinsic == TextIntrinsic::Push ? BuiltinType::Char : BuiltinType::Str
                )
            );
            if (!built) {
                return std::unexpected(built.error());
            }
            append_pending_failures(pending, built->pending_failures);
            completes &= built->completes;
            operands.push_back(std::move(built->argument));
        }
        return finish(
            draft().intern_builtin_type(*text_intrinsic_builtin_result(intrinsic)),
            SemTextIntrinsic {.intrinsic = intrinsic, .operands = std::move(operands)},
            std::nullopt,
            span,
            std::move(pending),
            completes
        );
    }

    auto member(const ASTMemberExpr& source, Value operand, Span span) noexcept
        -> AnalysisResult<Result> {
        return body.select_member(source, span, std::move(operand));
    }

    auto member_call(
        const ASTCallExpr& source,
        const ASTMemberExpr& member,
        Value operand,
        Span span
    ) noexcept -> AnalysisResult<Value> {
        auto callee =
            body.select_member(member, syntax().expression(source.callee).span, std::move(operand));
        if (!callee.has_value()) {
            return std::unexpected(callee.error());
        }
        return body.call_expression(source, span, std::move(*callee));
    }

    auto call(const ASTCallExpr& source, Span span) noexcept -> AnalysisResult<Value> {
        return body.call_expression(source, span);
    }

private:
    auto finish(
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

    auto external_operation(
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

    BodyElaborator& body;
    bool allow_pointer_narrowing;
};
