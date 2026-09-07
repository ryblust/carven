module carven:semantic.analysis.expr.interpret;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.storage;
import :frontend.literal;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.diagnostics;
import :semantic.analysis.operations;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import std;

struct NotConstant final {};

using ConstantExpressionResult = std::variant<ConstantID, NotConstant>;

template<typename Site>
auto interpret_expression(
    Site& site,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> AnalysisResult<typename Site::Result>;

template<typename Site>
auto fold_expression_constant(
    Site& site,
    std::expected<ConstantFact, ConstantEvaluationFailure> fact,
    Span span
) noexcept -> AnalysisResult<std::optional<ConstantID>> {
    if (fact.has_value()) {
        return site.draft().intern_constant(std::move(*fact));
    }
    if (const auto diagnostic = constant_evaluation_diagnostic(fact.error())) {
        return std::unexpected(site.fail(span, diagnostic->code, std::string(diagnostic->message)));
    }
    return std::nullopt;
}

template<typename Site>
auto interpret_literal(
    Site& site,
    const ASTLiteral& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    LiteralSign sign = LiteralSign::Positive
) noexcept -> AnalysisResult<typename Site::Value> {
    if (const auto* value = std::get_if<CStringLiteralValue>(&source.value)) {
        return site.c_string(value->bytes, span);
    }
    auto fact = normalize_literal(site.draft(), source, expected, sign);
    if (!fact.has_value()) {
        const auto diagnostic = constant_evaluation_diagnostic(fact.error());
        return std::unexpected(site.fail(
            source.span,
            diagnostic.has_value() ? diagnostic->code : DiagnosticCode::ConstLiteralRange,
            diagnostic.has_value() ? std::string(diagnostic->message) : "invalid literal value"
        ));
    }
    return site.constant(site.draft().intern_constant(std::move(*fact)), span);
}

template<typename Site>
auto interpret_unary(
    Site& site,
    const ASTPrefixExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<typename Site::Value> {
    auto literal_id = source.operand_id;
    while (const auto* group =
               std::get_if<ASTGroupExpr>(&site.syntax().expression(literal_id).value)) {
        literal_id = group->expression;
    }
    const auto& syntax = site.syntax().expression(literal_id);
    if (const auto* number = std::get_if<ASTLiteral>(&syntax.value);
        source.op == ASTPrefixOperator::Negate
        && number != nullptr
        && (std::holds_alternative<IntegerLiteralValue>(number->value)
            || std::holds_alternative<FloatingLiteralValue>(number->value))) {
        return interpret_literal(site, *number, span, expected, LiteralSign::Negative);
    }
    auto operand = site.read(
        source.operand_id,
        source.op == ASTPrefixOperator::LogicalNot
            ? std::optional<ConstructionTypeRef>(
                  site.draft().intern_builtin_type(BuiltinType::Bool)
              )
            : expected
    );
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    if (!site.present(*operand)) {
        return site.unavailable();
    }
    const auto operation = semantic_operator(source.op);
    if (site.external(site.type(*operand))) {
        return site.external_unary(operation, std::move(*operand), span);
    }
    const auto decision = decide_unary_operator(site.draft(), operation, site.type(*operand));
    if (!decision.has_value()) {
        return std::unexpected(site.fail(
            source.operator_span,
            decision.error().code,
            std::string(decision.error().message)
        ));
    }
    const auto builtin = operator_result_builtin(*decision);
    const auto type = builtin.has_value()
        ? ConstructionTypeRef {site.draft().intern_builtin_type(*builtin)}
        : site.type(*operand);
    auto known = std::optional<ConstantID>();
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        auto value = fold_expression_constant(
            site,
            fold_unary_constant(site.draft(), operation, site.known(*operand), *concrete),
            source.operator_span
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = *value;
    }
    return site.finish_unary(operation, type, std::move(*operand), known, span);
}

template<typename Site>
auto require_expression_boolean(Site& site, typename Site::Value& value, Span span) noexcept
    -> AnalysisResult<void> {
    const auto boolean = ConstructionTypeRef {site.draft().intern_builtin_type(BuiltinType::Bool)};
    if (site.external(site.type(value))) {
        auto converted = site.convert_argument(value, boolean, span);
        if (!converted.has_value()) {
            return std::unexpected(converted.error());
        }
    }
    if (!type_shapes_compatible(site.draft(), site.type(value), boolean)) {
        return std::unexpected(
            site.fail(span, DiagnosticCode::TypeConditionBool, "condition must have type bool")
        );
    }
    return {};
}

template<typename Site>
auto interpret_binary(
    Site& site,
    const ASTBinaryExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<typename Site::Value> {
    if (source.op == ASTBinaryOperator::LogicalAnd || source.op == ASTBinaryOperator::LogicalOr) {
        const auto boolean =
            ConstructionTypeRef {site.draft().intern_builtin_type(BuiltinType::Bool)};
        auto left = site.read(source.left, boolean);
        if (!left.has_value()) {
            return std::unexpected(left.error());
        }
        if (!site.present(*left)) {
            return site.unavailable();
        }
        auto checked =
            require_expression_boolean(site, *left, site.syntax().expression(source.left).span);
        if (!checked.has_value()) {
            return std::unexpected(checked.error());
        }
        const auto and_operation = source.op == ASTBinaryOperator::LogicalAnd;
        const auto truth = [&](const auto& value) noexcept -> std::optional<bool> {
            const auto known = site.known(value);
            if (!known.has_value()) {
                return std::nullopt;
            }
            const auto fact = site.draft().constant_copy(*known);
            const auto* boolean = std::get_if<BooleanConstant>(&fact.value);
            return boolean == nullptr ? std::nullopt : std::optional(boolean->value);
        };
        const auto left_truth = truth(*left);
        [[maybe_unused]] const auto execution =
            site.enter_operand_execution(!left_truth.has_value() || *left_truth == and_operation);
        auto right = site.read(source.right, boolean);
        if (!right.has_value()) {
            return std::unexpected(right.error());
        }
        if (!site.present(*right)) {
            return site.unavailable();
        }
        checked =
            require_expression_boolean(site, *right, site.syntax().expression(source.right).span);
        if (!checked.has_value()) {
            return std::unexpected(checked.error());
        }
        const auto right_truth = truth(*right);
        auto result = std::optional<bool>();
        if (left_truth.has_value()) {
            result = *left_truth == and_operation ? right_truth : left_truth;
        } else if (right_truth.has_value() && *right_truth != and_operation) {
            result = right_truth;
        }
        auto known = std::optional<ConstantID>();
        if (result.has_value()) {
            known = site.draft().intern_constant(
                {.type = std::get<TypeID>(boolean), .value = BooleanConstant {.value = *result}}
            );
        }
        return site
            .finish_short_circuit(and_operation, std::move(*left), std::move(*right), known, span);
    }
    auto left = std::optional<typename Site::Value>();
    auto right = std::optional<typename Site::Value>();
    if (binary_operand_plan(site.syntax(), source) == BinaryOperandPlan::LeftExpectedFromRight) {
        auto value = site.read(source.right, std::nullopt);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        if (!site.present(*value)) {
            return site.unavailable();
        }
        right.emplace(std::move(*value));
        value = site.read(
            source.left,
            site.external(site.type(*right)) ? std::nullopt : std::optional(site.type(*right))
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        if (!site.present(*value)) {
            return site.unavailable();
        }
        left.emplace(std::move(*value));
    } else {
        auto value = site.read(source.left, expected);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        if (!site.present(*value)) {
            return site.unavailable();
        }
        left.emplace(std::move(*value));
        value = site.read(
            source.right,
            site.external(site.type(*left)) ? std::nullopt : std::optional(site.type(*left))
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        if (!site.present(*value)) {
            return site.unavailable();
        }
        right.emplace(std::move(*value));
    }
    const auto operation = *semantic_operator(source.op);
    if (site.external(site.type(*left)) || site.external(site.type(*right))) {
        return site.external_binary(operation, std::move(*left), std::move(*right), span);
    }
    const auto decision = decide_binary_operator(
        site.draft(),
        operation,
        site.type(*left),
        site.type(*right),
        type_shapes_compatible(site.draft(), site.type(*left), site.type(*right)),
        !binary_operator_requires_equality(source.op) || site.supports_equality(site.type(*left))
    );
    if (!decision.has_value()) {
        return std::unexpected(site.fail(
            source.operator_span,
            decision.error().code,
            std::string(decision.error().message)
        ));
    }
    const auto builtin = operator_result_builtin(*decision);
    const auto type = builtin.has_value()
        ? ConstructionTypeRef {site.draft().intern_builtin_type(*builtin)}
        : site.type(*left);
    auto known = std::optional<ConstantID>();
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        auto value = fold_expression_constant(
            site,
            fold_binary_constant(
                site.draft(),
                operation,
                site.known(*left),
                site.known(*right),
                *concrete
            ),
            source.operator_span
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = *value;
    }
    return site.finish_binary(operation, type, std::move(*left), std::move(*right), known, span);
}

template<typename Site>
auto interpret_cast(Site& site, const ASTCastExpr& source, Span span) noexcept
    -> AnalysisResult<typename Site::Value> {
    auto operand = site.read(source.operand_id, std::nullopt);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    if (!site.present(*operand)) {
        return site.unavailable();
    }
    auto target = site.resolve_type(source.target_type);
    if (!target.has_value()) {
        return std::unexpected(target.error());
    }
    if (site.external(site.type(*operand)) || site.external(*target)) {
        return site.external_cast(*target, std::move(*operand), span);
    }
    const auto decision = decide_cast(
        site.draft(),
        site.type(*operand),
        *target,
        site.numeric_enum(site.type(*operand))
    );
    if (!decision.has_value()) {
        return std::unexpected(site.fail(
            source.operator_span,
            decision.error().code,
            std::string(decision.error().message)
        ));
    }
    auto known = std::optional<ConstantID>();
    if (const auto* concrete = std::get_if<TypeID>(&*target)) {
        auto value = fold_expression_constant(
            site,
            fold_cast_constant(site.draft(), *decision, site.known(*operand), *concrete),
            source.operator_span
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = *value;
    }
    return site.finish_cast(*decision, *target, std::move(*operand), known, span);
}

template<typename Site>
auto expected_expression_enum(
    Site& site,
    std::optional<ConstructionTypeRef> expected,
    Span span
) noexcept -> AnalysisResult<TypeID> {
    const auto* type = expected.has_value() ? std::get_if<TypeID>(&*expected) : nullptr;
    if (type == nullptr
        || !std::holds_alternative<EnumTypeValue>(site.draft().type_copy(*type).value)) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "contextual enum case requires an expected enum type"
        ));
    }
    return *type;
}

template<typename Site>
auto interpret_enum_case(
    Site& site,
    TypeID type,
    std::string_view name,
    Span name_span,
    std::span<const ASTCallArgument> arguments,
    Span span,
    bool called
) noexcept -> AnalysisResult<typename Site::Value> {
    auto selected = site.resolve_enum_case(type, name, name_span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    if (!called) {
        if (selected->payload_types.empty()) {
            return site.constant(*selected->constant, span);
        }
        return site.enum_constructor(type, *selected, span);
    }
    if (selected->payload_types.empty()) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeEnumCaseArity,
            "nullary enum case is a value and cannot be called"
        ));
    }
    if (arguments.size() != selected->payload_types.size()) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeEnumCaseArity,
            "enum case payload arity does not match"
        ));
    }
    auto payload = std::vector<typename Site::Value>();
    auto constants = std::vector<ConstantID>();
    for (const auto& [argument, type] : std::views::zip(arguments, selected->payload_types)) {
        auto value = site.read(argument.expression, type);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        if (!site.present(*value)) {
            return site.unavailable();
        }
        auto converted =
            site.convert_argument(*value, type, site.syntax().expression(argument.expression).span);
        if (!converted.has_value()) {
            return std::unexpected(converted.error());
        }
        if (const auto known = site.known(*value)) {
            constants.push_back(*known);
        }
        payload.push_back(std::move(*value));
    }
    auto known = std::optional<ConstantID>();
    if (constants.size() == payload.size()) {
        known = site.draft().intern_constant(
            {.type = type,
             .value =
                 PayloadEnumConstant {.enum_case = selected->id, .payload = std::move(constants)}}
        );
    }
    return site.finish_enum_case(type, selected->id, std::move(payload), known, span);
}

template<typename Site>
auto interpret_text(
    Site& site,
    TextIntrinsic intrinsic,
    typename Site::Value operand,
    Span span
) noexcept -> AnalysisResult<typename Site::Value> {
    const auto type = site.draft().intern_builtin_type(text_intrinsic_result(intrinsic));
    auto known = fold_expression_constant(
        site,
        fold_text_intrinsic_constant(site.draft(), intrinsic, site.known(operand), type),
        span
    );
    if (!known.has_value()) {
        return std::unexpected(known.error());
    }
    return site.finish_text(intrinsic, type, std::move(operand), *known, span);
}

template<typename Site>
auto interpret_member(Site& site, const ASTMemberExpr& source, Span span) noexcept
    -> AnalysisResult<typename Site::Result> {
    if (source.op == ASTMemberOperator::Scope) {
        auto type = site.resolve_enum_qualifier(source.operand_id);
        if (!type.has_value()) {
            return std::unexpected(type.error());
        }
        if (!type->has_value()) {
            return site.invalid_enum_qualifier(site.syntax().expression(source.operand_id).span);
        }
        return interpret_enum_case(
            site,
            **type,
            site.spelling(source.name_span),
            source.name_span,
            {},
            span,
            false
        );
    }
    auto operand = site.read(source.operand_id, std::nullopt);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    if (!site.present(*operand)) {
        return site.unavailable();
    }
    const auto operand_type = site.type(*operand);
    const auto* concrete = std::get_if<TypeID>(&operand_type);
    if (concrete != nullptr
        && site.draft().type_copy(*concrete).value
            == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}}) {
        auto decision = decide_text_property(site.spelling(source.name_span));
        if (!decision.has_value()) {
            return std::unexpected(site.fail(
                source.name_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        return interpret_text(site, *decision, std::move(*operand), span);
    }
    return site.member(source, std::move(*operand), span);
}

template<typename Site>
auto interpret_call(
    Site& site,
    const ASTCallExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<typename Site::Value> {
    const auto& callee = site.syntax().expression(source.callee);
    if (const auto* contextual = std::get_if<ASTContextualCaseExpr>(&callee.value)) {
        auto type = expected_expression_enum(site, expected, contextual->name_span);
        if (!type.has_value()) {
            return std::unexpected(type.error());
        }
        return interpret_enum_case(
            site,
            *type,
            site.spelling(contextual->name_span),
            contextual->name_span,
            source.arguments,
            span,
            true
        );
    }
    if (const auto* member = std::get_if<ASTMemberExpr>(&callee.value)) {
        if (member->op == ASTMemberOperator::Scope) {
            auto type = site.resolve_enum_qualifier(member->operand_id);
            if (!type.has_value()) {
                return std::unexpected(type.error());
            }
            if (!type->has_value()) {
                return site.invalid_enum_qualifier(
                    site.syntax().expression(member->operand_id).span
                );
            }
            return interpret_enum_case(
                site,
                **type,
                site.spelling(member->name_span),
                member->name_span,
                source.arguments,
                span,
                true
            );
        }
        auto operand = site.read(member->operand_id, std::nullopt);
        if (!operand.has_value()) {
            return std::unexpected(operand.error());
        }
        if (!site.present(*operand)) {
            return site.unavailable();
        }
        const auto decision = decide_text_method(
            site.draft(),
            site.type(*operand),
            site.spelling(member->name_span),
            source.arguments.size()
        );
        if (!decision.has_value()) {
            return std::unexpected(site.fail(
                decision.error().code == DiagnosticCode::TypeStrMethodArity ? span
                                                                            : member->name_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        if (decision->has_value()) {
            return interpret_text(site, **decision, std::move(*operand), span);
        }
        return site.member_call(source, *member, std::move(*operand), span);
    }
    return site.call(source, span);
}

template<typename Site>
auto interpret_expression(
    Site& site,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<typename Site::Result> {
    const auto& source = site.syntax().expression(expression);
    if (!site.admits(source)) {
        return site.unavailable();
    }
    return std::visit(
        [&](const auto& form) noexcept -> AnalysisResult<typename Site::Result> {
            using Form = std::remove_cvref_t<decltype(form)>;
            if constexpr (std::same_as<Form, ASTLiteral>) {
                return interpret_literal(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTGroupExpr>) {
                return interpret_expression(site, form.expression, expected);
            } else if constexpr (std::same_as<Form, ASTPrefixExpr>) {
                return interpret_unary(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTBinaryExpr>) {
                return interpret_binary(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTCastExpr>) {
                return interpret_cast(site, form, source.span);
            } else if constexpr (std::same_as<Form, ASTContextualCaseExpr>) {
                auto type = expected_expression_enum(site, expected, form.name_span);
                if (!type.has_value()) {
                    return std::unexpected(type.error());
                }
                return interpret_enum_case(
                    site,
                    *type,
                    site.spelling(form.name_span),
                    form.name_span,
                    {},
                    source.span,
                    false
                );
            } else if constexpr (std::same_as<Form, ASTMemberExpr>) {
                return interpret_member(site, form, source.span);
            } else if constexpr (std::same_as<Form, ASTCallExpr>) {
                return interpret_call(site, form, source.span, expected);
            } else {
                return site.extension(form, source.span, expected);
            }
        },
        source.value
    );
}
