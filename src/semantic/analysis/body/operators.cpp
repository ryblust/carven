module carven:semantic.analysis.body.operators.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.constant.proof;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

auto BodyElaborator::prefix_expression(
    const ASTPrefixExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    if (source.op == ASTPrefixOperator::Negate) {
        const auto& operand_source = ast.expression(source.operand_id);
        if (const auto* literal = std::get_if<ASTLiteral>(&operand_source.value); literal != nullptr
            && (std::holds_alternative<IntegerLiteralValue>(literal->value)
                || std::holds_alternative<FloatingLiteralValue>(literal->value))) {
            auto normalized = normalize_literal(draft(), *literal, expected, LiteralSign::Negative);
            if (!normalized.has_value()) {
                const auto diagnostic = constant_evaluation_diagnostic(normalized.error());
                return std::unexpected(fail(
                    operand_source.span,
                    diagnostic.has_value() ? diagnostic->code : DiagnosticCode::ConstLiteralRange,
                    diagnostic.has_value() ? std::string(diagnostic->message)
                                           : "negative literal is out of range"
                ));
            }
            const auto constant = draft().intern_constant(normalized->constant);
            const auto value = active_builder().append_value(
                normalized->constant.type,
                active_builder().lifetime(),
                SemLiteral {.value = normalized->literal},
                origin(span)
            );
            return BuiltExpression {
                .type = normalized->constant.type,
                .storage = value,
                .constant = constant,
                .pending_failures = {},
            };
        }
    }
    auto operand = expression(
        source.operand_id,
        source.op == ASTPrefixOperator::LogicalNot
            ? std::optional<ConstructionTypeRef>(draft().intern_builtin_type(BuiltinType::Bool))
            : expected
    );
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto pending_failures = take_pending(*operand);
    const auto operation = semantic_operator(source.op);
    const auto decision = decide_unary_operator(draft(), operation, operand->type);
    if (!decision.has_value()) {
        return std::unexpected(
            fail(source.operator_span, decision.error().code, std::string(decision.error().message))
        );
    }
    const auto result_type = decision.value() == OperatorResult::Boolean
        ? ConstructionTypeRef {draft().intern_builtin_type(BuiltinType::Bool)}
        : operand->type;
    if (const auto* concrete = std::get_if<TypeID>(&result_type); concrete != nullptr) {
        auto folded = fold_unary_constant(draft(), operation, operand->constant, *concrete);
        if (folded.has_value()) {
            auto result = publish_constant(std::move(*folded), span);
            result.pending_failures = std::move(pending_failures);
            return result;
        } else if (const auto diagnostic = constant_evaluation_diagnostic(folded.error())) {
            return std::unexpected(
                fail(source.operator_span, diagnostic->code, std::string(diagnostic->message))
            );
        }
    }
    auto operand_value =
        as_value(*operand, ast.expression(source.operand_id).span, AccessMode::Read);
    if (!operand_value.has_value()) {
        return std::unexpected(operand_value.error());
    }
    const auto value = active_builder().append_value(
        result_type,
        active_builder().lifetime(),
        expression_construction::Unary {.operation = operation, .operand = *operand_value},
        origin(span)
    );
    return BuiltExpression {
        .type = result_type,
        .storage = value,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = operand->completes,
    };
}

auto BodyElaborator::access_expression(const ASTAccessExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto operand = expression(source.operand_id);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto pending_failures = take_pending(*operand);
    const auto access = [&]() noexcept {
        switch (source.mode) {
            case ASTAccessMode::Read:  return AccessMode::Read;
            case ASTAccessMode::Write: return AccessMode::Write;
            case ASTAccessMode::Take:  return AccessMode::Take;
        }
        std::unreachable();
    }();
    if (access == AccessMode::Write) {
        auto place = as_place(*operand, span);
        if (!place.has_value()) {
            return std::unexpected(place.error());
        }
        operand->storage = *place;
        operand->pending_failures = std::move(pending_failures);
        return *operand;
    }
    auto value = as_value(*operand, access == AccessMode::Take ? source.marker_span : span, access);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    operand->storage = *value;
    operand->constant.reset();
    operand->pending_failures = std::move(pending_failures);
    return *operand;
}

auto BodyElaborator::binary_expression(
    const ASTBinaryExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    if (source.op == ASTBinaryOperator::LogicalAnd || source.op == ASTBinaryOperator::LogicalOr) {
        auto left = expression(source.left, draft().intern_builtin_type(BuiltinType::Bool));
        if (!left.has_value()) {
            return std::unexpected(left.error());
        }
        auto pending_failures = take_pending(*left);
        const auto left_constant = known_boolean_constant(draft(), left->constant);
        auto condition = require_bool(*left, ast.expression(source.left).span);
        if (!condition.has_value()) {
            return std::unexpected(condition.error());
        }
        const auto and_operation = source.op == ASTBinaryOperator::LogicalAnd;
        [[maybe_unused]] const auto path = ReferencePathGuard(
            reference_path_reachable,
            !left_constant.has_value() || *left_constant == and_operation
        );
        auto right = expression(source.right, draft().intern_builtin_type(BuiltinType::Bool));
        if (!right.has_value()) {
            return std::unexpected(right.error());
        }
        collect_pending(pending_failures, *right);
        const auto right_constant = known_boolean_constant(draft(), right->constant);
        auto right_value = require_bool(*right, ast.expression(source.right).span);
        if (!right_value.has_value()) {
            return std::unexpected(right_value.error());
        }
        const auto result_type = draft().intern_builtin_type(BuiltinType::Bool);
        auto result = make_built(
            result_type,
            SemShortCircuit<ConstructionTypeRef, FailureTermID> {
                UniqueIndirect(take_built(*left, ast.expression(source.left).span)),
                and_operation ? ShortCircuitOperator::And : ShortCircuitOperator::Or,
                UniqueIndirect(take_built(*right, ast.expression(source.right).span))
            },
            span,
            std::move(pending_failures)
        );
        auto result_constant = std::optional<bool>();
        if (left_constant.has_value()) {
            result_constant = *left_constant == and_operation ? right_constant : left_constant;
        } else if (right_constant.has_value() && *right_constant != and_operation) {
            result_constant = right_constant;
        }
        if (result_constant.has_value()) {
            result.constant = draft().intern_constant(
                ConstantFact {
                    .type = result_type,
                    .value = BooleanConstant {.value = *result_constant},
                }
            );
        }
        result.completes = left->completes && (left_constant == !and_operation || right->completes);
        return result;
    }

    auto left = std::optional<BuiltExpression>();
    auto right = std::optional<BuiltExpression>();
    auto left_pending = PendingFailureTerms();
    if (binary_operand_plan(ast, source) == BinaryOperandPlan::LeftExpectedFromRight) {
        // Type context can be resolved in either order; owned operands retain
        // the source program's left-to-right evaluation order.
        auto built_right = expression(source.right);
        if (!built_right.has_value()) {
            return std::unexpected(built_right.error());
        }
        right.emplace(std::move(*built_right));
        auto built_left = expression(source.left, right->type);
        if (!built_left.has_value()) {
            return std::unexpected(built_left.error());
        }
        left.emplace(std::move(*built_left));
        left_pending = take_pending(*left);
        auto left_value = as_value(*left, ast.expression(source.left).span, AccessMode::Read);
        if (!left_value.has_value()) {
            return std::unexpected(left_value.error());
        }
        left->storage = *left_value;
    } else {
        auto built_left = expression(source.left, expected);
        if (!built_left.has_value()) {
            return std::unexpected(built_left.error());
        }
        left.emplace(std::move(*built_left));
        auto built_right = expression(source.right, left->type);
        if (!built_right.has_value()) {
            return std::unexpected(built_right.error());
        }
        right.emplace(std::move(*built_right));
        left_pending = take_pending(*left);
    }
    auto pending_failures = std::move(left_pending);
    append_pending(pending_failures, take_pending(*right));
    const auto operation = semantic_operator(source.op);
    if (!operation.has_value()) {
        invariant_violation("logical operator reached ordinary binary lowering");
    }
    const auto operands_compatible = compatible(left->type, right->type);
    const auto equality = type_supports_equality(draft(), left->type);
    const auto decision = decide_binary_operator(
        draft(),
        *operation,
        left->type,
        right->type,
        operands_compatible,
        equality
    );
    if (!decision.has_value()) {
        return std::unexpected(
            fail(source.operator_span, decision.error().code, std::string(decision.error().message))
        );
    }
    const auto result_type = decision.value() == OperatorResult::Boolean
        ? ConstructionTypeRef {draft().intern_builtin_type(BuiltinType::Bool)}
        : left->type;
    if (const auto* concrete = std::get_if<TypeID>(&result_type); concrete != nullptr) {
        auto folded =
            fold_binary_constant(draft(), *operation, left->constant, right->constant, *concrete);
        if (folded.has_value()) {
            auto result = publish_constant(std::move(*folded), span);
            result.pending_failures = std::move(pending_failures);
            return result;
        } else if (const auto diagnostic = constant_evaluation_diagnostic(folded.error())) {
            return std::unexpected(
                fail(source.operator_span, diagnostic->code, std::string(diagnostic->message))
            );
        }
    }
    auto left_value = as_value(*left, ast.expression(source.left).span, AccessMode::Read);
    if (!left_value.has_value()) {
        return std::unexpected(left_value.error());
    }
    auto right_value = as_value(*right, ast.expression(source.right).span, AccessMode::Read);
    if (!right_value.has_value()) {
        return std::unexpected(right_value.error());
    }
    const auto value = active_builder().append_value(
        result_type,
        active_builder().lifetime(),
        expression_construction::Binary {
            .left = *left_value,
            .operation = *operation,
            .right = *right_value,
        },
        origin(span)
    );
    return BuiltExpression {
        .type = result_type,
        .storage = value,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = left->completes && right->completes,
    };
}

auto BodyElaborator::cast_expression(const ASTCastExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto operand = expression(source.operand_id);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto target = resolve_type(source.target_type);
    if (!target.has_value()) {
        return std::unexpected(target.error());
    }
    auto pending_failures = take_pending(*operand);
    auto source_is_enum = false;
    if (const auto* concrete = std::get_if<TypeID>(&operand->type)) {
        source_is_enum = std::holds_alternative<EnumTypeValue>(draft().type_copy(*concrete).value);
    }
    auto decision = decide_cast(draft(), operand->type, *target, source_is_enum);
    if (!decision.has_value()) {
        return std::unexpected(
            fail(source.operator_span, decision.error().code, std::string(decision.error().message))
        );
    }
    if (const auto* concrete = std::get_if<TypeID>(&*target); concrete != nullptr) {
        auto folded = fold_cast_constant(draft(), *decision, operand->constant, *concrete);
        if (folded.has_value()) {
            auto result = publish_constant(std::move(*folded), span);
            result.pending_failures = std::move(pending_failures);
            return result;
        } else if (const auto diagnostic = constant_evaluation_diagnostic(folded.error())) {
            return std::unexpected(fail(span, diagnostic->code, std::string(diagnostic->message)));
        }
    }
    auto value = as_value(*operand, ast.expression(source.operand_id).span, AccessMode::Read);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    const auto result = active_builder().append_value(
        *target,
        active_builder().lifetime(),
        expression_construction::Cast {.operand = *value, .kind = *decision},
        origin(span)
    );
    return BuiltExpression {
        .type = *target,
        .storage = result,
        .constant = std::nullopt,
        .pending_failures = std::move(pending_failures),
        .completes = operand->completes,
    };
}


} // namespace body_elaboration
