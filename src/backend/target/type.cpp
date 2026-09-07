module carven:backend.target.type.impl;

import :backend.target.type;
import :support.invariant;
import :support.visit;
import std;

namespace {
auto valid_query(const TargetExpr& expression) noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const TargetNameExpr&) static noexcept { return true; },
            [](const TargetIntrinsicNameExpr&) static noexcept { return true; },
            [](const TargetCallExpr& value) static noexcept {
                return valid_query(*value.callee)
                    && std::ranges::all_of(value.arguments, valid_query);
            },
            [](const TargetMemberExpr& value) static noexcept {
                return std::holds_alternative<TargetIdentifier>(value.name)
                    && valid_query(*value.operand);
            },
            [](const TargetIndexExpr& value) static noexcept {
                return valid_query(*value.operand) && valid_query(*value.index);
            },
            [](const TargetPrefixExpr& value) static noexcept {
                return (value.op == TargetPrefixOperator::Negate
                        || value.op == TargetPrefixOperator::LogicalNot
                        || value.op == TargetPrefixOperator::BitwiseNot)
                    && valid_query(*value.operand);
            },
            [](const TargetBinaryExpr& value) static noexcept {
                return valid_query(*value.left) && valid_query(*value.right);
            },
            [](const auto&) static noexcept { return false; }
        },
        expression.value
    );
}

auto equal_query(const TargetExpr& left, const TargetExpr& right) noexcept -> bool {
    if (left.value.index() != right.value.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& value) noexcept -> bool {
            using Value = std::remove_cvref_t<decltype(value)>;
            const auto& other = std::get<Value>(right.value);
            if constexpr (std::same_as<Value, TargetNameExpr>) {
                return value.name == other.name;
            } else if constexpr (std::same_as<Value, TargetIntrinsicNameExpr>) {
                return value.symbol == other.symbol;
            } else if constexpr (std::same_as<Value, TargetCallExpr>) {
                return equal_query(*value.callee, *other.callee)
                    && value.template_argument_type_ids == other.template_argument_type_ids
                    && std::ranges::equal(value.arguments, other.arguments, equal_query);
            } else if constexpr (std::same_as<Value, TargetMemberExpr>) {
                return std::get<TargetIdentifier>(value.name)
                    == std::get<TargetIdentifier>(other.name)
                    && equal_query(*value.operand, *other.operand);
            } else if constexpr (std::same_as<Value, TargetIndexExpr>) {
                return equal_query(*value.operand, *other.operand)
                    && equal_query(*value.index, *other.index);
            } else if constexpr (std::same_as<Value, TargetPrefixExpr>) {
                return value.op == other.op && equal_query(*value.operand, *other.operand);
            } else if constexpr (std::same_as<Value, TargetBinaryExpr>) {
                return value.op == other.op
                    && equal_query(*value.left, *other.left)
                    && equal_query(*value.right, *other.right);
            } else {
                std::unreachable();
            }
        },
        left.value
    );
}
}

TargetDeducedType::TargetDeducedType(TargetExpr expression) noexcept
    : queried_expression(std::move(expression)) {
    if (!valid_query(queried_expression)) {
        invariant_violation("invalid target type query expression");
    }
}

auto TargetDeducedType::expression() const noexcept -> const TargetExpr& {
    return queried_expression;
}

auto TargetDeducedType::operator==(const TargetDeducedType& other) const noexcept -> bool {
    return equal_query(queried_expression, other.queried_expression);
}
