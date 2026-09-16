module carven:semantic.analysis.expr.scalar;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.storage;
import :frontend.literal;
import :semantic.analysis.constant.literal;
import :semantic.analysis.expr.conversion;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.expr.text;
import :semantic.analysis.operations;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import std;

template<typename Site, typename Fold>
auto fold_expression_constant(Site& site, Fold fold, Span span) noexcept
    -> ExpressionResult<std::optional<ConstantID>> {
    if constexpr ((Site::mode == ExpressionMode::RequiredRoot)) {
        return std::nullopt;
    }
    auto fact = fold();
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
) noexcept -> ExpressionResult<typename Site::Value> {
    if (const auto* value = std::get_if<CStringLiteralValue>(&source.value)) {
        return site.c_string(value->bytes, span);
    }
    if (std::holds_alternative<NullPointerLiteralValue>(source.value)) {
        const auto* type = expected ? std::get_if<TypeID>(&*expected) : nullptr;
        if (type == nullptr
            || !std::holds_alternative<PointerTypeValue>(site.draft().type_copy(*type).value)) {
            return std::unexpected(
                site.fail(span, DiagnosticCode::TypeMismatch, "nullptr requires a ptr type context")
            );
        }
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
    auto value = site.constant(site.draft().intern_constant(std::move(*fact)), span);
    if (std::holds_alternative<StringLiteralValue>(source.value)
        && expected
        && *expected
            == ConstructionTypeRef(site.draft().intern_builtin_type(BuiltinType::String))) {
        return construct_text_value(
            site,
            TextIntrinsic::FromStr,
            site.draft().intern_builtin_type(BuiltinType::String),
            std::move(value),
            std::nullopt,
            span
        );
    }
    return value;
}

template<typename Site>
auto interpret_unary(
    Site& site,
    const ASTPrefixExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<typename Site::Value> {
    if (source.op == ASTPrefixOperator::Dereference) {
        return site.dereference(source, span);
    }
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
            [&]() noexcept {
                return fold_unary_constant(
                    site.draft(),
                    operation,
                    site.known(*operand),
                    *concrete
                );
            },
            source.operator_span
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = *value;
    }
    auto state = typename Site::OperandState();
    auto value = site.consume_read(state, std::move(*operand), source.operator_span);
    if (!value) {
        return std::unexpected(value.error());
    }
    return site.finish_constructed(
        type,
        SemUnary {.operation = operation, .operand = OwnedSemanticExpression(std::move(*value))},
        std::move(state),
        source.operator_span,
        known
    );
}

template<typename Site>
auto require_expression_boolean(Site& site, typename Site::Value& value, Span span) noexcept
    -> ExpressionResult<void> {
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
) noexcept -> ExpressionResult<typename Site::Value> {
    if (source.op == ASTBinaryOperator::LogicalAnd || source.op == ASTBinaryOperator::LogicalOr) {
        const auto boolean =
            ConstructionTypeRef {site.draft().intern_builtin_type(BuiltinType::Bool)};
        auto left = site.read(source.left, boolean);
        if (!left.has_value()) {
            return std::unexpected(left.error());
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
            const auto& fact = site.draft().constant(*known);
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
        if (result.has_value() && !(Site::mode == ExpressionMode::RequiredRoot)) {
            known = site.draft().intern_constant(
                {.type = std::get<TypeID>(boolean), .value = BooleanConstant {.value = *result}}
            );
        }
        return site.finish_short_circuit(
            and_operation,
            std::move(*left),
            std::move(*right),
            known,
            source.operator_span
        );
    }
    auto left = std::optional<typename Site::Value>();
    auto right = std::optional<typename Site::Value>();
    if (binary_operand_plan(site.syntax(), source) == BinaryOperandPlan::LeftExpectedFromRight) {
        auto value = site.read(source.right, std::nullopt);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        right.emplace(std::move(*value));
        value = site.read(
            source.left,
            site.external(site.type(*right)) ? std::nullopt : std::optional(site.type(*right))
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        left.emplace(std::move(*value));
    } else {
        auto value = site.read(source.left, expected);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        left.emplace(std::move(*value));
        value = site.read(
            source.right,
            site.external(site.type(*left)) ? std::nullopt : std::optional(site.type(*left))
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        right.emplace(std::move(*value));
    }
    const auto operation = *semantic_operator(source.op);
    const auto left_pointer = pointer_shape(site.draft(), site.type(*left));
    const auto right_pointer = pointer_shape(site.draft(), site.type(*right));
    if ((left_pointer || right_pointer) && (!left_pointer || !right_pointer)) {
        return std::unexpected(site.fail(
            source.operator_span,
            DiagnosticCode::TypeBinary,
            "ptr operations require two typed pointer operands"
        ));
    }
    if (site.external(site.type(*left)) || site.external(site.type(*right))) {
        return site.external_binary(operation, std::move(*left), std::move(*right), span);
    }
    const auto pointer_equality =
        (operation == BinaryOperator::Equal || operation == BinaryOperator::NotEqual)
        && (pointer_narrows(site.draft(), site.type(*left), site.type(*right))
            || pointer_narrows(site.draft(), site.type(*right), site.type(*left)));
    const auto decision = decide_binary_operator(
        site.draft(),
        operation,
        site.type(*left),
        site.type(*right),
        pointer_equality
            || type_shapes_compatible(site.draft(), site.type(*left), site.type(*right)),
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
            [&]() noexcept {
                return fold_binary_constant(
                    site.draft(),
                    operation,
                    site.known(*left),
                    site.known(*right),
                    *concrete
                );
            },
            source.operator_span
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = *value;
    }
    auto state = typename Site::OperandState();
    auto first = site.consume_read(state, std::move(*left), source.operator_span);
    if (!first) {
        return std::unexpected(first.error());
    }
    auto second = site.consume_read(state, std::move(*right), source.operator_span);
    if (!second) {
        return std::unexpected(second.error());
    }
    return site.finish_constructed(
        type,
        SemBinary {
            .left = OwnedSemanticExpression(std::move(*first)),
            .operation = operation,
            .right = OwnedSemanticExpression(std::move(*second))
        },
        std::move(state),
        source.operator_span,
        known
    );
}

template<typename Site>
auto interpret_cast(Site& site, const ASTCastExpr& source, Span span) noexcept
    -> ExpressionResult<typename Site::Value> {
    auto literal_id = source.operand_id;
    while (const auto* group =
               std::get_if<ASTGroupExpr>(&site.syntax().expression(literal_id).value)) {
        literal_id = group->expression;
    }
    if (const auto* literal = std::get_if<ASTLiteral>(&site.syntax().expression(literal_id).value);
        literal != nullptr && std::holds_alternative<NullPointerLiteralValue>(literal->value)) {
        auto target = site.resolve_type(source.target_type);
        if (!target) {
            return std::unexpected(target.error());
        }
        return site.read(source.operand_id, *target);
    }
    auto operand = site.read(source.operand_id, std::nullopt);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto target = site.resolve_type(source.target_type);
    if (!target.has_value()) {
        return std::unexpected(target.error());
    }
    if (site.external(site.type(*operand)) || site.external(*target)) {
        return site.external_cast(*target, std::move(*operand), span);
    }
    if (site.type(*operand)
            == ConstructionTypeRef(site.draft().intern_builtin_type(BuiltinType::Str))
        && *target == ConstructionTypeRef(site.draft().intern_builtin_type(BuiltinType::String))) {
        return construct_text_value(
            site,
            TextIntrinsic::FromStr,
            site.draft().intern_builtin_type(BuiltinType::String),
            std::move(*operand),
            std::nullopt,
            span
        );
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
    if (*decision == CastKind::Identity) {
        const auto known = site.known(*operand);
        return construct_cast(
            site,
            *decision,
            *target,
            std::move(*operand),
            known,
            source.operator_span
        );
    }
    auto known = std::optional<ConstantID>();
    if (const auto* concrete = std::get_if<TypeID>(&*target)) {
        auto value = fold_expression_constant(
            site,
            [&]() noexcept {
                return fold_cast_constant(site.draft(), *decision, site.known(*operand), *concrete);
            },
            source.operator_span
        );
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = *value;
    }
    return construct_cast(
        site,
        *decision,
        *target,
        std::move(*operand),
        known,
        source.operator_span
    );
}

template<typename Site>
auto interpret_range(
    Site& site,
    const ASTRangeExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<typename Site::Value> {
    auto element = std::optional<ConstructionTypeRef>();
    if (expected) {
        if (const auto* concrete = std::get_if<TypeID>(&*expected)) {
            const auto type = site.draft().type_copy(*concrete);
            if (const auto* range = std::get_if<RangeTypeValue>(&type.value)) {
                element = range->element;
            }
        }
    }
    auto begin = site.read(source.begin, element);
    if (!begin) {
        return std::unexpected(begin.error());
    }
    const auto begin_type = site.type(*begin);
    const auto* concrete = std::get_if<TypeID>(&begin_type);
    if (concrete == nullptr) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeRangeInteger,
            "range requires concrete integer bounds"
        ));
    }
    const auto type = site.draft().type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
        return std::unexpected(
            site.fail(span, DiagnosticCode::TypeRangeInteger, "range bounds must be integers")
        );
    }
    const auto range_type =
        site.draft().intern_type({.value = RangeTypeValue {.element = *concrete}});
    auto end = site.read(source.end, site.type(*begin));
    if (!end) {
        return std::unexpected(end.error());
    }
    if (!type_shapes_compatible(site.draft(), site.type(*begin), site.type(*end))) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeRangeBounds,
            "range bounds must have one compatible type"
        ));
    }
    auto known = std::optional<ConstantID>();
    if (site.known(*begin) && site.known(*end)) {
        const auto left = site.draft().constant(*site.known(*begin));
        const auto right = site.draft().constant(*site.known(*end));
        known = site.draft().intern_constant(
            {.type = range_type,
             .value = RangeConstant {
                 .begin = std::get<IntegerConstant>(left.value),
                 .end = std::get<IntegerConstant>(right.value),
                 .inclusive = source.inclusive
             }}
        );
    }
    auto state = typename Site::OperandState();
    auto first = site.consume_read(state, std::move(*begin), span);
    if (!first) {
        return std::unexpected(first.error());
    }
    auto last = site.consume_read(state, std::move(*end), span);
    if (!last) {
        return std::unexpected(last.error());
    }
    return site.finish_constructed(
        range_type,
        SemRange {
            .begin = OwnedSemanticExpression(std::move(*first)),
            .end = OwnedSemanticExpression(std::move(*last)),
            .inclusive = source.inclusive
        },
        std::move(state),
        span,
        known
    );
}
