module carven:backend.target.expr.impl;

import :backend.target.expr;
import :backend.target.stmt;
import :support.unique_indirect;
import std;

auto bool_expression(bool value) noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = value}};
}

auto binary_expression(TargetExpr left, TargetBinaryOperator operation, TargetExpr right) noexcept
    -> TargetExpr {
    return {
        .value = TargetBinaryExpr {
            .left = UniqueIndirect(std::move(left)),
            .op = operation,
            .right = UniqueIndirect(std::move(right)),
        },
    };
}

auto prefix_expression(TargetPrefixOperator operation, TargetExpr operand) noexcept -> TargetExpr {
    return {
        .value = TargetPrefixExpr {
            .op = operation,
            .operand = UniqueIndirect(std::move(operand)),
        },
    };
}

auto template_call_expression(
    TargetExpr callee,
    std::vector<TargetTypeID> template_arguments,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr {
    return {
        .value = TargetCallExpr {
            .callee = UniqueIndirect(std::move(callee)),
            .template_argument_type_ids = std::move(template_arguments),
            .arguments = std::move(arguments),
        },
    };
}

auto call_member(
    TargetExpr owner,
    std::string_view member,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr {
    auto callee = TargetExpr {
        .value = TargetMemberExpr {
            .operand = UniqueIndirect(std::move(owner)),
            .name = TargetIdentifier::from_spelling(member)
        }
    };
    return template_call_expression(std::move(callee), {}, std::move(arguments));
}
