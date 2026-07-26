module carven:backend.emission.render.stmt.impl;

import :backend.emission.render;
import :backend.target.raw;
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

} // namespace

auto TargetRenderer::render_statement(TargetStmtID id) noexcept -> LayoutNodeID {
    const auto& statement = unit.statement(id);
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
                auto prefix = std::string {};
                if (value.maybe_unused) {
                    prefix += "[[maybe_unused]] ";
                }
                if (value.binding == TargetVariableBinding::ConstValue
                    || value.binding == TargetVariableBinding::ConstReference) {
                    prefix += "const ";
                }
                auto suffix = std::string {};
                if (value.binding == TargetVariableBinding::ConstReference
                    || value.binding == TargetVariableBinding::MutableReference) {
                    suffix = "&";
                } else if (value.binding == TargetVariableBinding::RvalueReference) {
                    suffix = "&&";
                }
                const auto left = concat(
                    {text(prefix),
                     render_type(value.type),
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
                if (value.scoped) {
                    return render_statement_block(value.statements);
                }
                auto children = std::vector<LayoutNodeID> {};
                for (const auto child : value.statements) {
                    children.push_back(render_statement(child));
                }
                return stack(children);
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
                    {text(value.op == TargetUpdateOperator::Increment ? "++" : "--"),
                     render_expression(value.target, TargetPrecedence::Prefix),
                     text(";")}
                );
            },
            [&](const TargetBreakStmt&) noexcept { return text("break;"); },
            [&](const TargetContinueStmt&) noexcept { return text("continue;"); },
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
                for (const auto child : value.body) {
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
            [&](const TargetForStmt& value) noexcept {
                const auto initializer = value.initializer.has_value()
                    ? render_for_clause(*value.initializer)
                    : text("");
                const auto condition =
                    value.condition.has_value() ? render_expression(*value.condition) : text("");
                auto steps = std::vector<LayoutNodeID>();
                for (const auto step : value.steps) {
                    steps.push_back(render_for_clause(step));
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
            [&](const TargetRangeForStmt& value) noexcept {
                auto binding = std::string(value.maybe_unused ? "[[maybe_unused]] " : "");
                if (value.binding_mode != TargetRangeBindingMode::MutableReference) {
                    binding += "const ";
                }
                auto suffix = std::string {};
                if (value.binding_mode != TargetRangeBindingMode::ReadValue) {
                    suffix = "&";
                }
                const auto header = concat(
                    {text(binding),
                     render_type(value.type),
                     text(suffix),
                     text(" "),
                     render_identifier(value.name),
                     text(" : "),
                     render_expression(value.iterable)}
                );
                const auto arguments = std::array {header};
                return concat(
                    {text("for "),
                     delimited_list(arguments, "(", ")"),
                     text(" "),
                     render_statement_block(value.body)}
                );
            },
            [&](const TargetRawFragment& value) noexcept { return render_raw_fragment(value); },
        },
        statement.value
    );
    return with_attribution(rendered, statement.attribution);
}

auto TargetRenderer::render_for_clause(TargetStmtID id) noexcept -> LayoutNodeID {
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
                auto prefix = std::string(value.maybe_unused ? "[[maybe_unused]] " : "");
                if (value.binding == TargetVariableBinding::ConstValue
                    || value.binding == TargetVariableBinding::ConstReference) {
                    prefix += "const ";
                }
                auto suffix = std::string();
                if (value.binding == TargetVariableBinding::ConstReference
                    || value.binding == TargetVariableBinding::MutableReference) {
                    suffix = "&";
                } else if (value.binding == TargetVariableBinding::RvalueReference) {
                    suffix = "&&";
                }
                return concat(
                    {text(prefix),
                     render_type(value.type),
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
                    {text(value.op == TargetUpdateOperator::Increment ? "++" : "--"),
                     render_expression(value.target, TargetPrecedence::Prefix)}
                );
            },
            [&](const auto&) noexcept -> LayoutNodeID { std::unreachable(); },
        },
        unit.statement(id).value
    );
}
