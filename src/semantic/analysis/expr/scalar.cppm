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
import :semantic.analysis.types.display;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.simd;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import std;

template<typename Site>
auto interpret_literal(
    Site& site,
    const ASTLiteral& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    LiteralSign sign = LiteralSign::Positive
) noexcept -> ExpressionResult<typename Site::Value> {
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
        && *expected == ConstructionTypeRef(site.draft().builtin_type(BuiltinType::String))) {
        return construct_text_value(
            site,
            TextIntrinsic::FromStr,
            site.draft().builtin_type(BuiltinType::String),
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
) noexcept -> ExpressionTask<typename Site::Value> {
    if (source.op == ASTPrefixOperator::Dereference) {
        co_return (co_await site.dereference(source, span));
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
        co_return interpret_literal(site, *number, span, expected, LiteralSign::Negative);
    }
    auto operand = (co_await site.read(
        source.operand_id,
        source.op == ASTPrefixOperator::LogicalNot
            ? std::optional<ConstructionTypeRef>(site.draft().builtin_type(BuiltinType::Bool))
            : expected
    ));
    if (!operand.has_value()) {
        co_return std::unexpected(operand.error());
    }
    const auto operation = semantic_operator(source.op);
    if (site.external(site.type(*operand))) {
        co_return site.external_unary(operation, std::move(*operand), span);
    }
    const auto decision = decide_unary_operator(site.draft(), operation, site.type(*operand));
    if (!decision.has_value()) {
        co_return std::unexpected(site.fail(
            source.operator_span,
            decision.error().code,
            std::format(
                "{}, found '{}'",
                decision.error().message,
                type_display_name(site.draft(), site.type(*operand))
            )
        ));
    }
    const auto builtin = operator_result_builtin(*decision);
    const auto type = builtin.has_value()
        ? ConstructionTypeRef {site.draft().builtin_type(*builtin)}
        : site.type(*operand);
    const auto known = std::optional<ConstantID>();
    auto state = Site::operand_state();
    auto value = site.consume_read(state, std::move(*operand), source.operator_span);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    co_return site.finish_constructed(
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
    const auto boolean = ConstructionTypeRef {site.draft().builtin_type(BuiltinType::Bool)};
    if (site.external(site.type(value))) {
        auto converted = site.convert_argument(value, boolean, span);
        if (!converted.has_value()) {
            return std::unexpected(converted.error());
        }
    }
    if (!type_shapes_compatible(site.draft(), site.type(value), boolean)) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeConditionBool,
            std::format(
                "condition must have type bool, found '{}'",
                type_display_name(site.draft(), site.type(value))
            )
        ));
    }
    return {};
}

template<typename Site>
auto interpret_binary(
    Site& site,
    const ASTBinaryExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<typename Site::Value> {
    if (source.op == ASTBinaryOperator::LogicalAnd || source.op == ASTBinaryOperator::LogicalOr) {
        const auto boolean = ConstructionTypeRef {site.draft().builtin_type(BuiltinType::Bool)};
        auto left = (co_await site.read(source.left, boolean));
        if (!left.has_value()) {
            co_return std::unexpected(left.error());
        }
        auto checked =
            require_expression_boolean(site, *left, site.syntax().expression(source.left).span);
        if (!checked.has_value()) {
            co_return std::unexpected(checked.error());
        }
        const auto and_operation = source.op == ASTBinaryOperator::LogicalAnd;
        const auto truth = [&](std::optional<ConstantID> known) noexcept -> std::optional<bool> {
            if (!known.has_value()) {
                return std::nullopt;
            }
            const auto& fact = site.draft().constant(*known);
            const auto* boolean = std::get_if<BooleanConstant>(&fact.value);
            return boolean == nullptr ? std::nullopt : std::optional(boolean->value);
        };
        auto right = (co_await site.read(source.right, boolean));
        if (!right.has_value()) {
            co_return std::unexpected(right.error());
        }
        checked =
            require_expression_boolean(site, *right, site.syntax().expression(source.right).span);
        if (!checked.has_value()) {
            co_return std::unexpected(checked.error());
        }
        const auto source_left_truth = truth(site.known(*left));
        const auto right_truth = truth(site.known(*right));
        auto result = std::optional<bool>();
        if (source_left_truth.has_value()) {
            result = *source_left_truth == and_operation ? right_truth : source_left_truth;
        }
        auto known = std::optional<ConstantID>();
        if (result.has_value() && !(Site::mode == ExpressionMode::StaticRoot)) {
            known = site.draft().intern_constant(
                {.type = std::get<TypeID>(boolean), .value = BooleanConstant {.value = *result}}
            );
        }
        co_return site.finish_short_circuit(
            and_operation,
            std::move(*left),
            std::move(*right),
            known,
            source.operator_span
        );
    }
    const auto operand_context =
        [&](const typename Site::Value& operand) noexcept -> std::optional<ConstructionTypeRef> {
        if (site.external(site.type(operand))) {
            return std::nullopt;
        }
        const auto operand_type = site.type(operand);
        if (const auto* id = std::get_if<TypeID>(&operand_type)) {
            const auto canonical = site.draft().type_copy(*id);
            const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
            const auto layout = builtin ? simd_layout(builtin->kind) : std::nullopt;
            if (layout && builtin->kind == layout->vector) {
                return site.draft().builtin_type(layout->element);
            }
        }
        return site.type(operand);
    };
    auto left = std::optional<typename Site::Value>();
    auto right = std::optional<typename Site::Value>();
    if (binary_operand_plan(site.syntax(), source) == BinaryOperandPlan::LeftExpectedFromRight) {
        auto value = (co_await site.read(source.right, std::nullopt));
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        right.emplace(std::move(*value));
        value = (co_await site.read(source.left, operand_context(*right)));
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        left.emplace(std::move(*value));
    } else {
        auto value = (co_await site.read(source.left, expected));
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        left.emplace(std::move(*value));
        value = (co_await site.read(source.right, operand_context(*left)));
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        right.emplace(std::move(*value));
    }
    const auto operation = *semantic_operator(source.op);
    const auto left_pointer = pointer_shape(site.draft(), site.type(*left));
    const auto right_pointer = pointer_shape(site.draft(), site.type(*right));
    if ((left_pointer || right_pointer) && (!left_pointer || !right_pointer)) {
        co_return std::unexpected(site.fail(
            source.operator_span,
            DiagnosticCode::TypeBinary,
            "ptr operations require two typed pointer operands"
        ));
    }
    if (site.external(site.type(*left)) || site.external(site.type(*right))) {
        co_return site.external_binary(operation, std::move(*left), std::move(*right), span);
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
        co_return std::unexpected(site.fail(
            source.operator_span,
            decision.error().code,
            std::format(
                "{}: '{}' and '{}'",
                decision.error().message,
                type_display_name(site.draft(), site.type(*left)),
                type_display_name(site.draft(), site.type(*right))
            )
        ));
    }
    const auto builtin_kind = [&](const auto& input) noexcept -> std::optional<BuiltinType> {
        const auto input_type = site.type(input);
        const auto* concrete = std::get_if<TypeID>(&input_type);
        if (!concrete) {
            return std::nullopt;
        }
        const auto canonical = site.draft().type_copy(*concrete);
        const auto* value = std::get_if<BuiltinTypeValue>(&canonical.value);
        return value ? std::optional(value->kind) : std::nullopt;
    };
    const auto left_kind = builtin_kind(*left);
    const auto right_kind = builtin_kind(*right);
    const auto layout = left_kind && simd_layout(*left_kind) ? simd_layout(*left_kind)
        : right_kind                                         ? simd_layout(*right_kind)
                                                             : std::nullopt;
    const auto builtin = operator_result_builtin(
        *decision,
        layout ? std::optional(left_kind == layout->mask ? layout->mask : layout->vector)
               : std::nullopt
    );
    const auto type = builtin.has_value()
        ? ConstructionTypeRef {site.draft().builtin_type(*builtin)}
        : site.type(*left);
    if (layout && left_kind != layout->mask) {
        const auto splat =
            [&](typename Site::Value input,
                BuiltinType kind) noexcept -> ExpressionResult<typename Site::Value> {
            if (kind != layout->element) {
                return input;
            }
            auto splat_state = Site::operand_state();
            auto scalar = site.consume_read(splat_state, std::move(input), source.operator_span);
            if (!scalar) {
                return std::unexpected(scalar.error());
            }
            auto constant = std::optional<ConstantID>();
            if (scalar->constant) {
                const auto& fact = site.draft().constant(*scalar->constant);
                const auto bits = layout->element == BuiltinType::F32
                    ? std::bit_cast<std::uint32_t>(std::get<F32Constant>(fact.value).value)
                    : static_cast<std::uint32_t>(std::get<IntegerConstant>(fact.value).magnitude());
                constant = site.draft().intern_constant(
                    {.type = site.draft().builtin_type(layout->vector),
                     .value =
                         SIMDConstant {.lanes = std::vector<std::uint32_t>(layout->width, bits)}}
                );
            }
            auto inputs = std::vector<SemCallArgument>();
            inputs.push_back({.access = AccessMode::Read, .expression = std::move(*scalar)});
            return site.finish_constructed(
                site.draft().builtin_type(layout->vector),
                SemIntrinsic {.operation = SIMDIntrinsic::Splat, .operands = std::move(inputs)},
                std::move(splat_state),
                source.operator_span,
                constant
            );
        };
        auto first = splat(std::move(*left), *left_kind);
        if (!first) {
            co_return std::unexpected(first.error());
        }
        auto second = splat(std::move(*right), *right_kind);
        if (!second) {
            co_return std::unexpected(second.error());
        }
        left = std::move(*first);
        right = std::move(*second);
    }
    const auto known = std::optional<ConstantID>();
    auto state = Site::operand_state();
    auto first = site.consume_read(state, std::move(*left), source.operator_span);
    if (!first) {
        co_return std::unexpected(first.error());
    }
    auto second = site.consume_read(state, std::move(*right), source.operator_span);
    if (!second) {
        co_return std::unexpected(second.error());
    }
    co_return site.finish_constructed(
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
    -> ExpressionTask<typename Site::Value> {
    auto literal_id = source.operand_id;
    while (const auto* group =
               std::get_if<ASTGroupExpr>(&site.syntax().expression(literal_id).value)) {
        literal_id = group->expression;
    }
    if (const auto* literal = std::get_if<ASTLiteral>(&site.syntax().expression(literal_id).value);
        literal != nullptr && std::holds_alternative<NullPointerLiteralValue>(literal->value)) {
        auto target = (co_await site.resolve_type(source.target_type));
        if (!target) {
            co_return std::unexpected(target.error());
        }
        co_return (co_await site.read(source.operand_id, *target));
    }
    auto operand = (co_await site.read(source.operand_id, std::nullopt));
    if (!operand.has_value()) {
        co_return std::unexpected(operand.error());
    }
    auto target = (co_await site.resolve_type(source.target_type));
    if (!target.has_value()) {
        co_return std::unexpected(target.error());
    }
    if (site.external(site.type(*operand)) || site.external(*target)) {
        co_return site.external_cast(*target, std::move(*operand), span);
    }
    if (site.type(*operand) == ConstructionTypeRef(site.draft().builtin_type(BuiltinType::Str))
        && *target == ConstructionTypeRef(site.draft().builtin_type(BuiltinType::String))) {
        co_return construct_text_value(
            site,
            TextIntrinsic::FromStr,
            site.draft().builtin_type(BuiltinType::String),
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
        co_return std::unexpected(site.fail(
            source.operator_span,
            decision.error().code,
            std::format(
                "{} from '{}' to '{}'",
                decision.error().message,
                type_display_name(site.draft(), site.type(*operand)),
                type_display_name(site.draft(), *target)
            )
        ));
    }
    if (*decision == CastKind::Identity) {
        const auto known = site.known(*operand);
        co_return construct_cast(
            site,
            *decision,
            *target,
            std::move(*operand),
            known,
            source.operator_span
        );
    }
    const auto known = std::optional<ConstantID>();
    co_return construct_cast(
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
) noexcept -> ExpressionTask<typename Site::Value> {
    auto element = std::optional<ConstructionTypeRef>();
    if (expected) {
        if (const auto* concrete = std::get_if<TypeID>(&*expected)) {
            const auto type = site.draft().type_copy(*concrete);
            if (const auto* range = std::get_if<RangeTypeValue>(&type.value)) {
                element = range->element;
            }
        }
    }
    auto end = std::optional<typename Site::Value>();
    if (!element
        && numeric_operand_plan(site.syntax(), source.begin, source.end)
            == BinaryOperandPlan::LeftExpectedFromRight) {
        auto value = (co_await site.read(source.end, std::nullopt));
        if (!value) {
            co_return std::unexpected(value.error());
        }
        end.emplace(std::move(*value));
        element = site.type(*end);
    }
    auto begin = (co_await site.read(source.begin, element));
    if (!begin) {
        co_return std::unexpected(begin.error());
    }
    const auto begin_type = site.type(*begin);
    const auto* concrete = std::get_if<TypeID>(&begin_type);
    if (concrete == nullptr) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeRangeInteger,
            "range requires concrete integer bounds"
        ));
    }
    const auto type = site.draft().type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
        co_return std::unexpected(
            site.fail(span, DiagnosticCode::TypeRangeInteger, "range bounds must be integers")
        );
    }
    const auto range_type =
        site.draft().intern_type({.value = RangeTypeValue {.element = *concrete}});
    if (!end) {
        auto value = (co_await site.read(source.end, site.type(*begin)));
        if (!value) {
            co_return std::unexpected(value.error());
        }
        end.emplace(std::move(*value));
    }
    if (!type_shapes_compatible(site.draft(), site.type(*begin), site.type(*end))) {
        co_return std::unexpected(site.fail(
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
    auto state = Site::operand_state();
    auto first = site.consume_read(state, std::move(*begin), span);
    if (!first) {
        co_return std::unexpected(first.error());
    }
    auto last = site.consume_read(state, std::move(*end), span);
    if (!last) {
        co_return std::unexpected(last.error());
    }
    co_return site.finish_constructed(
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
