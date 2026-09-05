module carven:semantic.analysis.constant.proof.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.storage;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.constant.proof;
import :semantic.analysis.operations;
import :semantic.semir.constant;
import :semantic.semir.identity;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import std;

namespace {

auto source_id(const ProgramDraft& draft, ProgramModuleID module) noexcept -> SourceID {
    return draft.syntax_tree(module).view().source_id();
}

auto fail(
    const ProgramDraft& draft,
    ProgramModuleID module,
    Span span,
    DiagnosticCode code,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                         .primary(locate(source_id(draft, module), span))
                                         .build());
}

auto require_environment(const ConstantExpressionEnvironment& environment) noexcept -> void {
    if (!environment.resolve_name
        || !environment.resolve_enum_qualifier
        || !environment.resolve_enum_case
        || !environment.resolve_type
        || !environment.supports_equality
        || !environment.is_numeric_enum) {
        invariant_violation("constant expression environment is incomplete");
    }
}

auto concrete_type(ConstructionTypeRef type) noexcept -> std::optional<TypeID> {
    const auto* value = std::get_if<TypeID>(&type);
    return value == nullptr ? std::nullopt : std::optional(*value);
}

auto require_type_owner(
    ConstructionTypeRef type,
    ProgramIdentity owner,
    std::string_view message
) noexcept -> void {
    std::visit(
        [&](const auto id) noexcept {
            using ID = std::remove_cvref_t<decltype(id)>;
            static_assert(std::same_as<ID, TypeID> || std::same_as<ID, TypeTermID>);
            if (id.owner() != owner) {
                invariant_violation(message);
            }
        },
        type
    );
}

auto enum_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<TypeID> {
    const auto concrete = concrete_type(type);
    if (!concrete.has_value()) {
        return std::nullopt;
    }
    return std::holds_alternative<EnumTypeValue>(draft.type_copy(*concrete).value) ? concrete
                                                                                   : std::nullopt;
}

auto require_enum_case_contract(
    const ProgramDraft& draft,
    TypeID type,
    const ConstantEnumCase& enum_case
) noexcept -> void {
    if (enum_case.id.owner() != draft.identity() || enum_case.owner.owner() != draft.identity()) {
        invariant_violation("constant enum-case resolution crossed semantic program owners");
    }
    for (const auto payload : enum_case.payload_types) {
        require_type_owner(
            payload,
            draft.identity(),
            "constant enum-case resolution returned a foreign payload type"
        );
    }
    if (enum_case.constant.has_value() && enum_case.constant->owner() != draft.identity()) {
        invariant_violation("constant enum-case resolution returned a foreign constant");
    }
    const auto canonical = draft.type_copy(type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    if (nominal == nullptr || nominal->enumeration != enum_case.owner) {
        invariant_violation("constant enum-case resolution returned a case from another enum");
    }
}

auto require_enum_case_constant(const ConstantFact& fact, EnumCaseID enum_case) noexcept -> void {
    const auto matches = std::visit(
        [enum_case](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, NumericEnumConstant>
                          || std::same_as<Value, PayloadEnumConstant>) {
                return value.enum_case == enum_case;
            } else {
                static_assert(
                    std::same_as<Value, IntegerConstant>
                        || std::same_as<Value, BooleanConstant>
                        || std::same_as<Value, StringConstant>
                        || std::same_as<Value, F32Constant>
                        || std::same_as<Value, F64Constant>
                        || std::same_as<Value, CharacterConstant>,
                    "unhandled non-enum constant"
                );
                return false;
            }
        },
        fact.value
    );
    if (!matches) {
        invariant_violation("enum case constant disagrees with its declaration identity");
    }
}

auto adopt_expected(
    const ProgramDraft& draft,
    ProgramModuleID module,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    std::optional<ConstantFact> proof
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    if (!proof.has_value() || !expected.has_value()) {
        return proof;
    }
    if (!type_shapes_compatible(draft, *expected, ConstructionTypeRef {proof->type})) {
        return std::unexpected(fail(
            draft,
            module,
            span,
            DiagnosticCode::TypeMismatch,
            "expression has an incompatible type"
        ));
    }
    const auto concrete = concrete_type(*expected);
    if (!concrete.has_value()) {
        return std::optional<ConstantFact>();
    }
    proof->type = *concrete;
    return proof;
}

auto evaluation_result(
    const ProgramDraft& draft,
    ProgramModuleID module,
    Span span,
    std::expected<ConstantFact, ConstantEvaluationFailure> result
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    if (result.has_value()) {
        return std::optional(std::move(*result));
    }
    const auto diagnostic = constant_evaluation_diagnostic(result.error());
    if (!diagnostic.has_value()) {
        return std::optional<ConstantFact>();
    }
    return std::unexpected(
        fail(draft, module, span, diagnostic->code, std::string(diagnostic->message))
    );
}

auto literal_result(
    ProgramDraft& draft,
    ProgramModuleID module,
    const ASTLiteral& literal,
    std::optional<ConstructionTypeRef> expected,
    LiteralSign sign
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    auto normalized = normalize_literal(draft, literal, expected, sign);
    if (normalized.has_value()) {
        return adopt_expected(
            draft,
            module,
            literal.span,
            expected,
            std::optional(std::move(normalized->constant))
        );
    }
    if (const auto diagnostic = constant_evaluation_diagnostic(normalized.error())) {
        return std::unexpected(
            fail(draft, module, literal.span, diagnostic->code, std::string(diagnostic->message))
        );
    }
    return std::unexpected(fail(
        draft,
        module,
        literal.span,
        DiagnosticCode::TypeMismatch,
        "literal is incompatible with its expected type"
    ));
}

auto expected_enum_type(
    const ProgramDraft& draft,
    ProgramModuleID module,
    Span name_span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<TypeID> {
    if (!expected.has_value()) {
        return std::unexpected(fail(
            draft,
            module,
            name_span,
            DiagnosticCode::TypeEnumContext,
            "contextual enum case requires an expected enum type"
        ));
    }
    const auto type = enum_type(draft, *expected);
    if (!type.has_value()) {
        return std::unexpected(fail(
            draft,
            module,
            name_span,
            DiagnosticCode::TypeEnumContext,
            "contextual enum case requires an expected enum type"
        ));
    }
    return *type;
}

auto prove_empty_case(
    const ProgramDraft& draft,
    ProgramModuleID module,
    const ConstantExpressionEnvironment& environment,
    TypeID type,
    std::string_view name,
    Span name_span
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    auto resolved = environment.resolve_enum_case(type, name, name_span);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }
    require_enum_case_contract(draft, type, *resolved);
    if (!resolved->payload_types.empty()) {
        return std::unexpected(fail(
            draft,
            module,
            name_span,
            DiagnosticCode::TypeEnumCaseArity,
            "payload enum case must be called with its payload"
        ));
    }
    if (!resolved->constant.has_value()) {
        invariant_violation("nullary enum case has no constant fact");
    }
    auto fact = draft.constant_copy(*resolved->constant);
    if (fact.type != type) {
        invariant_violation("enum case constant disagrees with its nominal type");
    }
    require_enum_case_constant(fact, resolved->id);
    return std::optional(std::move(fact));
}

auto prove_form(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    const ASTExpr& source,
    const auto& form,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<std::optional<ConstantFact>>;

auto prove_payload_case(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    TypeID type,
    std::string_view name,
    Span name_span,
    const ASTCallExpr& call,
    Span call_span
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    auto resolved = environment.resolve_enum_case(type, name, name_span);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }
    require_enum_case_contract(draft, type, *resolved);
    if (resolved->payload_types.empty()) {
        return std::unexpected(fail(
            draft,
            module,
            call_span,
            DiagnosticCode::TypeEnumCaseArity,
            "nullary enum case is a value and cannot be called"
        ));
    }
    if (call.arguments.size() != resolved->payload_types.size()) {
        return std::unexpected(fail(
            draft,
            module,
            call_span,
            DiagnosticCode::TypeEnumCaseArity,
            "enum case payload arity does not match"
        ));
    }
    if (resolved->constant.has_value()) {
        invariant_violation("payload enum case unexpectedly owns a constant fact");
    }
    auto payload = std::vector<ConstantID>();
    payload.reserve(call.arguments.size());
    for (const auto [argument, payload_type] :
         std::views::zip(call.arguments, resolved->payload_types)) {
        auto child = prove_constant_expression(
            draft,
            module,
            syntax,
            environment,
            argument.expression,
            payload_type
        );
        if (!child.has_value()) {
            return std::unexpected(child.error());
        }
        if (!child->has_value()) {
            return std::optional<ConstantFact>();
        }
        payload.push_back(draft.intern_constant(std::move(**child)));
    }
    return std::optional(
        ConstantFact {
            .type = type,
            .value = PayloadEnumConstant {
                .enum_case = resolved->id,
                .payload = std::move(payload),
            },
        }
    );
}

auto prove_binary(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    const ASTExpr& source,
    const ASTBinaryExpr& binary,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    auto left = AnalysisResult<std::optional<ConstantFact>>(std::optional<ConstantFact>());
    auto right = AnalysisResult<std::optional<ConstantFact>>(std::optional<ConstantFact>());
    switch (binary_operand_plan(syntax, binary)) {
        case BinaryOperandPlan::LeftExpectedFromRight:
            right = prove_constant_expression(draft, module, syntax, environment, binary.right);
            if (right.has_value() && right->has_value()) {
                left = prove_constant_expression(
                    draft,
                    module,
                    syntax,
                    environment,
                    binary.left,
                    ConstructionTypeRef {(**right).type}
                );
            }
            break;
        case BinaryOperandPlan::RightExpectedFromLeft:
            left = prove_constant_expression(draft, module, syntax, environment, binary.left);
            if (left.has_value() && left->has_value()) {
                right = prove_constant_expression(
                    draft,
                    module,
                    syntax,
                    environment,
                    binary.right,
                    ConstructionTypeRef {(**left).type}
                );
            }
            break;
        case BinaryOperandPlan::Independent:
            left = prove_constant_expression(draft, module, syntax, environment, binary.left);
            if (left.has_value() && left->has_value()) {
                right = prove_constant_expression(draft, module, syntax, environment, binary.right);
            }
            break;
    }
    if (!left.has_value()) {
        return std::unexpected(left.error());
    }
    if (!right.has_value()) {
        return std::unexpected(right.error());
    }
    if (!left->has_value() || !right->has_value()) {
        return std::optional<ConstantFact>();
    }
    auto left_fact = std::move(**left);
    auto right_fact = std::move(**right);
    const auto compatible = type_shapes_compatible(
        draft,
        ConstructionTypeRef {left_fact.type},
        ConstructionTypeRef {right_fact.type}
    );
    const auto equality = !binary_operator_requires_equality(binary.op)
        || environment.supports_equality(ConstructionTypeRef {left_fact.type});
    const auto decision = decide_binary_operator(
        draft,
        binary.op,
        ConstructionTypeRef {left_fact.type},
        ConstructionTypeRef {right_fact.type},
        compatible,
        equality
    );
    if (!decision.has_value()) {
        return std::unexpected(fail(
            draft,
            module,
            binary.operator_span,
            decision.error().code,
            std::string(decision.error().message)
        ));
    }
    const auto result_builtin = operator_result_builtin(*decision);
    const auto result_type =
        result_builtin.has_value() ? draft.intern_builtin_type(*result_builtin) : left_fact.type;

    if (binary.op == ASTBinaryOperator::LogicalOr || binary.op == ASTBinaryOperator::LogicalAnd) {
        const auto* left_boolean = std::get_if<BooleanConstant>(&left_fact.value);
        const auto* right_boolean = std::get_if<BooleanConstant>(&right_fact.value);
        if (left_boolean == nullptr || right_boolean == nullptr) {
            return std::optional<ConstantFact>();
        }
        const auto value = binary.op == ASTBinaryOperator::LogicalOr
            ? left_boolean->value || right_boolean->value
            : left_boolean->value && right_boolean->value;
        return adopt_expected(
            draft,
            module,
            source.span,
            expected,
            std::optional(
                ConstantFact {
                    .type = result_type,
                    .value = BooleanConstant {.value = value},
                }
            )
        );
    }
    const auto operation = semantic_operator(binary.op);
    if (!operation.has_value()) {
        invariant_violation("non-logical constant binary expression has no SemIR operator");
    }
    auto result = evaluation_result(
        draft,
        module,
        binary.operator_span,
        evaluate_binary_constant_value(draft, *operation, left_fact, right_fact, result_type)
    );
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    return adopt_expected(draft, module, source.span, expected, std::move(*result));
}

auto prove_form(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    const ASTExpr& source,
    const auto& form,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    using Form = std::remove_cvref_t<decltype(form)>;
    if constexpr (std::same_as<Form, ASTLiteral>) {
        return literal_result(draft, module, form, expected, LiteralSign::Positive);
    } else if constexpr (std::same_as<Form, ASTGroupExpr>) {
        return prove_constant_expression(
            draft,
            module,
            syntax,
            environment,
            form.expression,
            expected
        );
    } else if constexpr (std::same_as<Form, ASTNameExpr>) {
        const auto name = draft.source_slice_copy(module, form.name_span);
        auto named = environment.resolve_name(name, form.name_span);
        if (!named.has_value()) {
            return std::unexpected(named.error());
        }
        require_type_owner(
            named->type,
            draft.identity(),
            "constant name resolution returned a foreign type"
        );
        if (!named->constant.has_value()) {
            return std::optional<ConstantFact>();
        }
        if (named->constant->owner() != draft.identity()) {
            invariant_violation("constant name resolution returned a foreign constant");
        }
        auto fact = draft.constant_copy(*named->constant);
        const auto named_type = concrete_type(named->type);
        if (!named_type.has_value() || *named_type != fact.type) {
            invariant_violation("constant name resolution returned a mismatched type and value");
        }
        return adopt_expected(draft, module, source.span, expected, std::optional(std::move(fact)));
    } else if constexpr (std::same_as<Form, ASTContextualCaseExpr>) {
        auto type = expected_enum_type(draft, module, form.name_span, expected);
        if (!type.has_value()) {
            return std::unexpected(type.error());
        }
        return prove_empty_case(
            draft,
            module,
            environment,
            *type,
            draft.source_slice_copy(module, form.name_span),
            form.name_span
        );
    } else if constexpr (std::same_as<Form, ASTMemberExpr>) {
        if (form.op != ASTMemberOperator::Scope) {
            return std::optional<ConstantFact>();
        }
        auto qualifier = environment.resolve_enum_qualifier(form.operand_id);
        if (!qualifier.has_value()) {
            return std::unexpected(qualifier.error());
        }
        if (!qualifier->has_value()) {
            return std::optional<ConstantFact>();
        }
        auto proof = prove_empty_case(
            draft,
            module,
            environment,
            **qualifier,
            draft.source_slice_copy(module, form.name_span),
            form.name_span
        );
        if (!proof.has_value()) {
            return std::unexpected(proof.error());
        }
        return adopt_expected(draft, module, source.span, expected, std::move(*proof));
    } else if constexpr (std::same_as<Form, ASTPrefixExpr>) {
        auto literal_id = form.operand_id;
        while (const auto* group =
                   std::get_if<ASTGroupExpr>(&syntax.expression(literal_id).value)) {
            literal_id = group->expression;
        }
        const auto* literal = std::get_if<ASTLiteral>(&syntax.expression(literal_id).value);
        if (form.op == ASTPrefixOperator::Negate
            && literal != nullptr
            && (std::holds_alternative<IntegerLiteralValue>(literal->value)
                || std::holds_alternative<FloatingLiteralValue>(literal->value))) {
            return literal_result(draft, module, *literal, expected, LiteralSign::Negative);
        }
        auto operand =
            prove_constant_expression(draft, module, syntax, environment, form.operand_id);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        if (!operand->has_value()) {
            return std::optional<ConstantFact>();
        }
        const auto operation = semantic_operator(form.op);
        const auto decision =
            decide_unary_operator(draft, operation, ConstructionTypeRef {(**operand).type});
        if (!decision.has_value()) {
            return std::unexpected(fail(
                draft,
                module,
                form.operator_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        const auto result_builtin = operator_result_builtin(*decision);
        const auto result_type = result_builtin.has_value()
            ? draft.intern_builtin_type(*result_builtin)
            : (**operand).type;
        auto result = evaluation_result(
            draft,
            module,
            form.operator_span,
            evaluate_unary_constant_value(draft, operation, **operand, result_type)
        );
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        return adopt_expected(draft, module, source.span, expected, std::move(*result));
    } else if constexpr (std::same_as<Form, ASTBinaryExpr>) {
        return prove_binary(draft, module, syntax, environment, source, form, expected);
    } else if constexpr (std::same_as<Form, ASTCastExpr>) {
        auto operand =
            prove_constant_expression(draft, module, syntax, environment, form.operand_id);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        if (!operand->has_value()) {
            return std::optional<ConstantFact>();
        }
        auto target = environment.resolve_type(form.target_type);
        if (!target.has_value()) {
            return std::unexpected(target.error());
        }
        const auto target_type = concrete_type(*target);
        const auto source_enum = enum_type(draft, ConstructionTypeRef {(**operand).type});
        const auto numeric_enum =
            source_enum.has_value() && environment.is_numeric_enum(*source_enum);
        const auto decision =
            decide_cast(draft, ConstructionTypeRef {(**operand).type}, *target, numeric_enum);
        if (!decision.has_value()) {
            return std::unexpected(fail(
                draft,
                module,
                form.operator_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        if (!target_type.has_value()) {
            return std::optional<ConstantFact>();
        }
        auto result = evaluation_result(
            draft,
            module,
            form.operator_span,
            evaluate_cast_constant_value(draft, *decision, **operand, *target_type)
        );
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        return adopt_expected(draft, module, source.span, expected, std::move(*result));
    } else if constexpr (std::same_as<Form, ASTCallExpr>) {
        const auto& callee = syntax.expression(form.callee);
        if (const auto* contextual = std::get_if<ASTContextualCaseExpr>(&callee.value)) {
            auto type = expected_enum_type(draft, module, contextual->name_span, expected);
            if (!type.has_value()) {
                return std::unexpected(type.error());
            }
            return prove_payload_case(
                draft,
                module,
                syntax,
                environment,
                *type,
                draft.source_slice_copy(module, contextual->name_span),
                contextual->name_span,
                form,
                source.span
            );
        }
        const auto* member = std::get_if<ASTMemberExpr>(&callee.value);
        if (member == nullptr) {
            return std::optional<ConstantFact>();
        }
        if (member->op == ASTMemberOperator::Scope) {
            auto qualifier = environment.resolve_enum_qualifier(member->operand_id);
            if (!qualifier.has_value()) {
                return std::unexpected(qualifier.error());
            }
            if (!qualifier->has_value()) {
                return std::optional<ConstantFact>();
            }
            auto proof = prove_payload_case(
                draft,
                module,
                syntax,
                environment,
                **qualifier,
                draft.source_slice_copy(module, member->name_span),
                member->name_span,
                form,
                source.span
            );
            if (!proof.has_value()) {
                return std::unexpected(proof.error());
            }
            return adopt_expected(draft, module, source.span, expected, std::move(*proof));
        }
        auto operand =
            prove_constant_expression(draft, module, syntax, environment, member->operand_id);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        if (!operand->has_value()) {
            return std::optional<ConstantFact>();
        }
        const auto name = draft.source_slice_copy(module, member->name_span);
        const auto decision = decide_text_method(
            draft,
            ConstructionTypeRef {(**operand).type},
            name,
            form.arguments.size()
        );
        if (!decision.has_value()) {
            return std::unexpected(fail(
                draft,
                module,
                decision.error().code == DiagnosticCode::TypeStrMethodArity ? source.span
                                                                            : member->name_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        if (!decision->has_value()) {
            return std::optional<ConstantFact>();
        }
        const auto result_type = draft.intern_builtin_type(text_intrinsic_result(**decision));
        auto result = evaluation_result(
            draft,
            module,
            source.span,
            evaluate_text_intrinsic_constant_value(draft, **decision, **operand, result_type)
        );
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        return adopt_expected(draft, module, source.span, expected, std::move(*result));
    } else if constexpr (std::same_as<Form, ASTArrayExpr>
                         || std::same_as<Form, ASTConstructionExpr>
                         || std::same_as<Form, ASTAccessExpr>
                         || std::same_as<Form, ASTIndexExpr>
                         || std::same_as<Form, ASTLambdaExpr>
                         || std::same_as<Form, ASTPropagationExpr>
                         || std::same_as<Form, ASTIfForm>
                         || std::same_as<Form, ASTMatchForm>
                         || std::same_as<Form, ASTTryForm>) {
        return std::optional<ConstantFact>();
    } else {
        static_assert(std::same_as<Form, void>, "new expression form needs a constant policy");
    }
}

} // namespace

auto prove_constant_expression(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<std::optional<ConstantFact>> {
    require_environment(environment);
    if (expected.has_value()) {
        require_type_owner(
            *expected,
            draft.identity(),
            "constant proof received a foreign expected type"
        );
    }
    if (syntax.source_id() != source_id(draft, module)) {
        invariant_violation("constant proof mixed a module with another syntax tree");
    }
    const auto& source = syntax.expression(expression);
    return std::visit(
        [&](const auto& form) noexcept {
            return prove_form(draft, module, syntax, environment, source, form, expected);
        },
        source.value
    );
}

auto prove_array_extent(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ConstantExpressionEnvironment& environment,
    ASTExprID expression
) noexcept -> AnalysisResult<std::uint64_t> {
    auto proof = prove_constant_expression(draft, module, syntax, environment, expression);
    if (!proof.has_value()) {
        return std::unexpected(proof.error());
    }
    const auto* constant =
        proof->has_value() ? std::get_if<IntegerConstant>(&(**proof).value) : nullptr;
    const auto span = syntax.expression(expression).span;
    if (constant == nullptr) {
        return std::unexpected(fail(
            draft,
            module,
            span,
            DiagnosticCode::ConstArrayExtent,
            "array extent must be a constant integer"
        ));
    }
    if (constant->negative()) {
        return std::unexpected(fail(
            draft,
            module,
            span,
            DiagnosticCode::ConstNegativeArrayExtent,
            "array extent cannot be negative"
        ));
    }
    return constant->magnitude();
}
