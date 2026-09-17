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
) noexcept -> ExpressionResult<typename Site::Selection> {
    const auto& source = site.syntax().expression(expression);
    if (!site.admits(source)) {
        return std::unexpected(ExpressionNotAdmitted {});
    }
    return source.value.visit(
        [&](const auto& form) noexcept -> ExpressionResult<typename Site::Selection> {
            using Form = std::remove_cvref_t<decltype(form)>;
            if constexpr (std::same_as<Form, ASTLiteral>) {
                return interpret_literal(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTGroupExpr>) {
                return interpret_expression(site, form.expression, expected);
            } else if constexpr (std::same_as<Form, ASTPrefixExpr>) {
                return interpret_unary(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTBinaryExpr>) {
                return interpret_binary(site, form, source.span, expected);
            } else if constexpr (std::same_as<Form, ASTRangeExpr>) {
                return interpret_range(site, form, source.span, expected);
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
        }
    );
}
