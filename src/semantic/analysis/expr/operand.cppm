module carven:semantic.analysis.expr.operand;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.semir.type;
import std;

struct CallArgumentOperand final {
    ASTExprID expression;
    AccessMode access;
};

auto call_argument_operand(ASTView syntax, ASTExprID expression) noexcept -> CallArgumentOperand;

template<typename Site>
auto read_value_argument(
    Site& site,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<typename Site::Value> {
    const auto span = site.syntax().expression(expression).span;
    const auto selected = call_argument_operand(site.syntax(), expression);
    if (selected.access != AccessMode::Read) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::AccessCallMismatch,
            "argument access marker differs from the parameter"
        ));
    }
    auto operand = site.read(selected.expression, expected);
    if (!operand) {
        return std::unexpected(operand.error());
    }
    if (site.type(*operand) == ConstructionTypeRef(site.draft().builtin_type(BuiltinType::Void))) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeValueRequired,
            "expression does not produce a value"
        ));
    }
    if (expected) {
        if (auto checked = site.convert_argument(*operand, *expected, span); !checked) {
            return std::unexpected(checked.error());
        }
    }
    return operand;
}
