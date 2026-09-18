module carven:semantic.analysis.expr.interpret;

import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.storage;
import :semantic.analysis.expr.call;
import :semantic.analysis.expr.member;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scalar;
import :semantic.semir.type;
import std;

template<typename Site>
auto interpret_expression(
    Site& site,
    ASTExprID expression,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> ExpressionTask<typename Site::Selection> {
    const auto& source = site.syntax().expression(expression);
    if (!site.admits(source)) {
        co_return std::unexpected(ExpressionNotAdmitted {});
    }
    co_return (co_await source.value.visit(
        [&](const auto& form) noexcept -> ExpressionTask<typename Site::Selection> {
            using Form = std::remove_cvref_t<decltype(form)>;
            if constexpr (std::same_as<Form, ASTLiteral>) {
                co_return interpret_literal(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTGroupExpr>) {
                co_return (co_await interpret_expression(site, form.expression, expected));
            } else if constexpr (std::same_as<Form, ASTPrefixExpr>) {
                co_return (co_await interpret_unary(site, form, source.span, expected));
            } else if constexpr (std::same_as<Form, ASTBinaryExpr>) {
                co_return (co_await interpret_binary(site, form, source.span, expected));
            } else if constexpr (std::same_as<Form, ASTRangeExpr>) {
                co_return (co_await interpret_range(site, form, source.span, expected));
            } else if constexpr (std::same_as<Form, ASTCastExpr>) {
                co_return (co_await interpret_cast(site, form, source.span));
            } else if constexpr (std::same_as<Form, ASTContextualCaseExpr>) {
                auto type = expected_expression_enum(site, expected, form.name_span);
                if (!type.has_value()) {
                    co_return std::unexpected(type.error());
                }
                co_return (co_await interpret_enum_case(
                    site,
                    *type,
                    site.spelling(form.name_span),
                    form.name_span,
                    {},
                    source.span,
                    false
                ));
            } else if constexpr (std::same_as<Form, ASTMemberExpr>) {
                co_return (co_await interpret_member(site, form, source.span));
            } else if constexpr (std::same_as<Form, ASTCallExpr>) {
                co_return (co_await interpret_call(site, form, source.span, expected));
            } else {
                co_return (co_await site.extension(form, source.span, expected));
            }
        }
    ));
}
