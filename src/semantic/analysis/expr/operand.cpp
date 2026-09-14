module carven:semantic.analysis.expr.operand.impl;

import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.operand;
import :semantic.semir.type;
import std;

auto call_argument_operand(ASTView syntax, ASTExprID expression) noexcept -> CallArgumentOperand {
    const auto* explicit_access = std::get_if<ASTAccessExpr>(&syntax.expression(expression).value);
    if (explicit_access == nullptr) {
        return {.expression = expression, .access = AccessMode::Read};
    }
    const auto access = [&]() noexcept {
        switch (explicit_access->mode) {
            case ASTAccessMode::Read:  return AccessMode::Read;
            case ASTAccessMode::Write: return AccessMode::Write;
            case ASTAccessMode::Take:  return AccessMode::Take;
        }
        std::unreachable();
    }();
    return {.expression = explicit_access->operand_id, .access = access};
}
