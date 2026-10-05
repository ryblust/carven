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
    if (const auto* form = std::get_if<ASTLiteral>(&source.value)) {
        co_return interpret_literal(site, *form, source.span, expected);
    }
    using ValueTask = ExpressionTask<typename Site::Value>;
    using SelectionTask = ExpressionTask<typename Site::Selection>;
    using DispatchedTask = std::variant<ValueTask, SelectionTask>;
    const auto value_task = [](ValueTask task) static noexcept {
        return DispatchedTask(std::in_place_index<0>, std::move(task));
    };
    const auto selection_task = [](SelectionTask task) static noexcept {
        return DispatchedTask(std::in_place_index<1>, std::move(task));
    };
    auto task = source.value.visit([&](const auto& form) noexcept -> DispatchedTask {
        using Form = std::remove_cvref_t<decltype(form)>;
        if constexpr (std::same_as<Form, ASTLiteral>) {
            std::unreachable();
        } else if constexpr (std::same_as<Form, ASTGroupExpr>) {
            return selection_task(interpret_expression(site, form.expression, expected));
        } else if constexpr (std::same_as<Form, ASTPrefixExpr>) {
            return value_task(interpret_unary(site, form, source.span, expected));
        } else if constexpr (std::same_as<Form, ASTBinaryExpr>) {
            return value_task(interpret_binary(site, form, source.span, expected));
        } else if constexpr (std::same_as<Form, ASTRangeExpr>) {
            return value_task(interpret_range(site, form, source.span, expected));
        } else if constexpr (std::same_as<Form, ASTCastExpr>) {
            return value_task(interpret_cast(site, form, source.span));
        } else if constexpr (std::same_as<Form, ASTContextualCaseExpr>) {
            return value_task(interpret_contextual_enum_case(site, form, source.span, expected));
        } else if constexpr (std::same_as<Form, ASTMemberExpr>) {
            return selection_task(interpret_member(site, form, source.span));
        } else if constexpr (std::same_as<Form, ASTCallExpr>) {
            return value_task(interpret_call(site, form, source.span, expected));
        } else {
            return selection_task(site.extension(form, source.span, expected));
        }
    });
    if (task.index() == 0uz) {
        co_return (co_await std::get<0>(std::move(task)));
    }
    co_return (co_await std::get<1>(std::move(task)));
}
