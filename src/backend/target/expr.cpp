module carven:backend.target.expr.impl;

import :backend.target.expr;
import :backend.target.stmt;
import :support.unique_indirect;
import std;

auto TargetStringLiteral::native_symbol() const noexcept -> std::optional<TargetSymbol> {
    switch (kind) {
        case TargetStringLiteralKind::String:     return std::nullopt;
        case TargetStringLiteralKind::StringView: return TargetSymbol::StdStringView;
    }
    std::unreachable();
}

auto TargetLiteralExpr::native_symbol() const noexcept -> std::optional<TargetSymbol> {
    return value.visit([](const auto& literal) static noexcept {
        return target_node_symbol(literal);
    });
}

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

auto co_await_expression(TargetExpr operand) noexcept -> TargetExpr {
    return {.value = TargetCoAwaitExpr {.operand = UniqueIndirect(std::move(operand))}};
}

auto template_name_expression(
    TargetExpr operand,
    std::vector<TargetTemplateArgument> arguments
) noexcept -> TargetExpr {
    return {
        .value = TargetTemplateNameExpr {
            .operand = UniqueIndirect(std::move(operand)),
            .arguments = std::move(arguments),
        },
    };
}

auto template_primary_expression(const TargetExpr& expression) noexcept -> const TargetExpr& {
    if (const auto* name = std::get_if<TargetTemplateNameExpr>(&expression.value)) {
        return *name->operand;
    }
    return expression;
}

auto template_call_expression(
    TargetExpr callee,
    std::vector<TargetTemplateArgument> template_arguments,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr {
    return call_expression(
        template_name_expression(std::move(callee), std::move(template_arguments)),
        std::move(arguments)
    );
}

auto call_expression(TargetExpr callee, std::vector<TargetExpr> arguments) noexcept -> TargetExpr {
    return {
        .value = TargetCallExpr {
            .callee = UniqueIndirect(std::move(callee)),
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
    return call_expression(std::move(callee), std::move(arguments));
}
