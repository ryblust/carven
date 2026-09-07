module carven:backend.emission.render.stmt.impl;

import :backend.emission.render;
import :support.visit;
import std;

namespace {

auto assignment_spelling(TargetAssignmentOperator op) noexcept -> std::string_view {
    switch (op) {
        case TargetAssignmentOperator::Assign:     return "=";
        case TargetAssignmentOperator::Add:        return "+=";
        case TargetAssignmentOperator::Subtract:   return "-=";
        case TargetAssignmentOperator::Multiply:   return "*=";
        case TargetAssignmentOperator::Divide:     return "/=";
        case TargetAssignmentOperator::Remainder:  return "%=";
        case TargetAssignmentOperator::BitwiseAnd: return "&=";
        case TargetAssignmentOperator::BitwiseOr:  return "|=";
        case TargetAssignmentOperator::BitwiseXor: return "^=";
        case TargetAssignmentOperator::LeftShift:  return "<<=";
        case TargetAssignmentOperator::RightShift: return ">>=";
    }
    std::unreachable();
}

auto update_spelling(TargetUpdateOperator op) noexcept -> std::string_view {
    switch (op) {
        case TargetUpdateOperator::Increment: return "++";
        case TargetUpdateOperator::Decrement: return "--";
    }
    std::unreachable();
}

} // namespace

auto TargetRenderer::render_statement(const TargetStmt& statement) noexcept -> LayoutNodeID {
    const auto rendered = std::visit(
        Overloaded {
            [&](const TargetExprStmt& value) noexcept {
                return concat({render_expression(value.expression), text(";")});
            },
            [&](const TargetDiscardStmt& value) noexcept {
                const auto argument = std::array {render_expression(value.expression)};
                return concat(
                    {text("static_cast<void>"), delimited_list(argument, "(", ")"), text(";")}
                );
            },
            [&](const TargetReturnStmt& value) noexcept {
                if (!value.expression.has_value()) {
                    return text("return;");
                }
                return concat({text("return "), render_expression(*value.expression), text(";")});
            },
            [&](const TargetVariableStmt& value) noexcept {
                const auto prefix =
                    value.maybe_unused ? std::string("[[maybe_unused]] ") : std::string();
                const auto constant = value.binding == TargetVariableBinding::ConstValue
                    || value.binding == TargetVariableBinding::ConstReference;
                auto suffix = std::string {};
                if (value.binding == TargetVariableBinding::ConstReference
                    || value.binding == TargetVariableBinding::MutableReference) {
                    suffix = "&";
                } else if (value.binding == TargetVariableBinding::RvalueReference) {
                    suffix = "&&";
                }
                const auto left = concat(
                    {text(prefix),
                     render_type(value.type, constant),
                     text(suffix),
                     text(" "),
                     render_identifier(value.name)}
                );
                const auto right = concat({text("= "), render_expression(value.initializer)});
                return concat(
                    {choice(
                         {concat({left, text(" "), right}),
                          concat(
                              {left, builder.indent(indent_width, concat({builder.line(), right}))}
                          )}
                     ),
                     text(";")}
                );
            },
            [&](const TargetBlockStmt& value) noexcept {
                return render_statement_block(value.statements);
            },
            [&](const TargetAssignmentStmt& value) noexcept {
                const auto left = render_expression(value.target);
                const auto right = concat(
                    {text(assignment_spelling(value.op)), text(" "), render_expression(value.value)}
                );
                return concat(
                    {choice(
                         {concat({left, text(" "), right}),
                          concat(
                              {left, builder.indent(indent_width, concat({builder.line(), right}))}
                          )}
                     ),
                     text(";")}
                );
            },
            [&](const TargetUpdateStmt& value) noexcept {
                return concat(
                    {text(update_spelling(value.op)),
                     render_expression(value.target, TargetPrecedence::Prefix),
                     text(";")}
                );
            },
            [&](const TargetBreakStmt&) noexcept { return text("break;"); },
            [&](const TargetContinueStmt&) noexcept { return text("continue;"); },
            [&](const TargetUnreachableStmt&) noexcept {
                return text("carven::runtime::unreachable();");
            },
            [&](const TargetRuntimeTrapStmt&) noexcept { return text("std::abort();"); },
            [&](const TargetGotoStmt& value) noexcept {
                return concat({text("goto "), render_identifier(value.label), text(";")});
            },
            [&](const TargetLabelStmt& value) noexcept {
                return concat({render_identifier(value.label), text(":;")});
            },
            [&](const TargetIfStmt& value) noexcept {
                auto result = std::vector<LayoutNodeID> {};
                for (auto index = 0uz; index < value.branches.size(); ++index) {
                    const auto& branch = value.branches[index];
                    const auto condition = std::array {render_expression(branch.condition)};
                    result.push_back(concat(
                        {text(index == 0 ? "if " : "else if "),
                         delimited_list(condition, "(", ")"),
                         text(" "),
                         render_statement_block(branch.body)}
                    ));
                }
                if (value.else_body.has_value()) {
                    result.push_back(
                        concat({text("else "), render_statement_block(*value.else_body)})
                    );
                }
                return builder.join(result, text(" "));
            },
            [&](const TargetWhileStmt& value) noexcept {
                auto body = std::vector<LayoutNodeID> {};
                for (const auto& child : value.body) {
                    body.push_back(render_statement(child));
                }
                const auto condition = std::array {render_expression(value.condition)};
                return concat(
                    {text("while "),
                     delimited_list(condition, "(", ")"),
                     text(" "),
                     braced_block(body)}
                );
            },
            [&](const TargetRangeForStmt& value) noexcept {
                return concat(
                    {text("for ("),
                     text(value.maybe_unused ? "[[maybe_unused]] " : ""),
                     render_type(value.type, value.binding == TargetVariableBinding::ConstValue),
                     text(value.binding == TargetVariableBinding::MutableReference ? "& " : " "),
                     text(value.name.spelling()),
                     text(" : "),
                     render_expression(value.range),
                     text(") "),
                     render_statement_block(value.body)}
                );
            },
            [&](const TargetForStmt& value) noexcept {
                const auto initializer = value.initializer.has_value()
                    ? render_for_initializer(*value.initializer)
                    : text("");
                const auto condition =
                    value.condition.has_value() ? render_expression(*value.condition) : text("");
                auto steps = std::vector<LayoutNodeID>();
                for (const auto& step : value.steps) {
                    steps.push_back(render_for_step(step));
                }
                const auto header = concat(
                    {initializer,
                     text("; "),
                     condition,
                     text("; "),
                     builder.join(steps, text(", "))}
                );
                const auto broken_header = concat(
                    {text("("),
                     builder.indent(
                         indent_width,
                         concat(
                             {builder.line(),
                              initializer,
                              text(";"),
                              builder.line(),
                              condition,
                              text(";"),
                              builder.line(),
                              builder.join(steps, text(", "))}
                         )
                     ),
                     builder.line(),
                     text(")")}
                );
                return concat(
                    {text("for "),
                     choice(
                         {concat({text("("), builder.flatten(header), text(")")}), broken_header}
                     ),
                     text(" "),
                     render_statement_block(value.body)}
                );
            },
        },
        statement.value
    );
    return with_attribution(rendered, statement.attribution);
}

auto TargetRenderer::render_for_initializer(const TargetForInitializer& initializer) noexcept
    -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](const TargetExprStmt& value) noexcept {
                return render_expression(value.expression);
            },
            [&](const TargetDiscardStmt& value) noexcept {
                return concat(
                    {text("static_cast<void>("), render_expression(value.expression), text(")")}
                );
            },
            [&](const TargetVariableStmt& value) noexcept {
                const auto prefix =
                    value.maybe_unused ? std::string("[[maybe_unused]] ") : std::string();
                const auto constant = value.binding == TargetVariableBinding::ConstValue
                    || value.binding == TargetVariableBinding::ConstReference;
                auto suffix = std::string();
                if (value.binding == TargetVariableBinding::ConstReference
                    || value.binding == TargetVariableBinding::MutableReference) {
                    suffix = "&";
                } else if (value.binding == TargetVariableBinding::RvalueReference) {
                    suffix = "&&";
                }
                return concat(
                    {text(prefix),
                     render_type(value.type, constant),
                     text(suffix),
                     text(" "),
                     render_identifier(value.name),
                     text(" = "),
                     render_expression(value.initializer)}
                );
            },
            [&](const TargetAssignmentStmt& value) noexcept {
                return concat(
                    {render_expression(value.target),
                     text(" "),
                     text(assignment_spelling(value.op)),
                     text(" "),
                     render_expression(value.value)}
                );
            },
            [&](const TargetUpdateStmt& value) noexcept {
                return concat(
                    {text(update_spelling(value.op)),
                     render_expression(value.target, TargetPrecedence::Prefix)}
                );
            },
        },
        initializer.value
    );
}

auto TargetRenderer::render_for_step(const TargetForStep& step) noexcept -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](const TargetExprStmt& value) noexcept {
                return render_expression(value.expression);
            },
            [&](const TargetDiscardStmt& value) noexcept {
                return concat(
                    {text("static_cast<void>("), render_expression(value.expression), text(")")}
                );
            },
            [&](const TargetAssignmentStmt& value) noexcept {
                return concat(
                    {render_expression(value.target),
                     text(" "),
                     text(assignment_spelling(value.op)),
                     text(" "),
                     render_expression(value.value)}
                );
            },
            [&](const TargetUpdateStmt& value) noexcept {
                return concat(
                    {text(update_spelling(value.op)),
                     render_expression(value.target, TargetPrecedence::Prefix)}
                );
            },
        },
        step.value
    );
}
