module carven:frontend.dump.ast.stmt.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.type;
import :frontend.dump.ast;
import :source.text;
import :support.visit;
import std;

auto ASTDumper::render_for_header(
    const ASTForHeader& header,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    std::visit(
        Overloaded {
            [&](const ASTRangeForHeader& range) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("header RangeForHeader {}", format_dump_span(header.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                if (range.write_marker.has_value()) {
                    render_span_field(nested_prefix, false, "write marker", *range.write_marker);
                } else {
                    append_line(nested_prefix, false, "write marker <absent>");
                }
                render_span_field(
                    nested_prefix,
                    false,
                    std::holds_alternative<ASTNamedBindingTarget>(range.target) ? "name"
                                                                                : "discard",
                    binding_target_span(range.target)
                );
                if (!range.type.has_value()) {
                    append_line(nested_prefix, false, "type <absent>");
                } else {
                    render_type(*range.type, nested_prefix, false, "type ");
                }
                std::visit(
                    Overloaded {
                        [&](ASTExprID iterable) noexcept {
                            render_expression(iterable, nested_prefix, true, "iterable ");
                        },
                        [&](const ASTHalfOpenRange& iterable) noexcept {
                            const auto iterable_span = Span::from_bounds(
                                ast.expression(iterable.begin).span.start(),
                                ast.expression(iterable.end).span.end()
                            );
                            append_line(
                                nested_prefix,
                                true,
                                std::format(
                                    "iterable HalfOpenRange {}",
                                    format_dump_span(iterable_span)
                                )
                            );
                            const auto iterable_prefix = child_prefix(nested_prefix, true);
                            render_expression(iterable.begin, iterable_prefix, false, "begin ");
                            render_span_field(
                                iterable_prefix,
                                false,
                                "operator",
                                iterable.operator_span
                            );
                            render_expression(iterable.end, iterable_prefix, true, "end ");
                        },
                    },
                    range.iterable
                );
            },
            [&](const ASTCStyleForHeader& c_style) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("header CStyleForHeader {}", format_dump_span(header.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                std::visit(
                    Overloaded {
                        [&](std::monostate) noexcept {
                            append_line(nested_prefix, false, "initializer <absent>");
                        },
                        [&](const ASTVariableDecl& declaration) noexcept {
                            render_variable_declaration(
                                declaration,
                                nested_prefix,
                                false,
                                c_style.initializer.span
                            );
                        },
                        [&](const ASTAssignment& assignment) noexcept {
                            render_assignment(
                                assignment,
                                nested_prefix,
                                false,
                                c_style.initializer.span
                            );
                        },
                        [&](ASTExprID expression) noexcept {
                            render_expression(expression, nested_prefix, false, "initializer ");
                        },
                    },
                    c_style.initializer.value
                );
                if (!c_style.condition.has_value()) {
                    append_line(nested_prefix, false, "condition <absent>");
                } else {
                    render_expression(*c_style.condition, nested_prefix, false, "condition ");
                }
                render_list(
                    nested_prefix,
                    true,
                    "steps",
                    c_style.steps,
                    [&](const ASTForStep& step,
                        std::string_view item_prefix,
                        bool item_last) noexcept {
                        std::visit(
                            Overloaded {
                                [&](const ASTAssignment& assignment) noexcept {
                                    render_assignment(
                                        assignment,
                                        item_prefix,
                                        item_last,
                                        step.span
                                    );
                                },
                                [&](const ASTUpdate& update) noexcept {
                                    render_update(update, item_prefix, item_last, step.span);
                                },
                                [&](ASTExprID expression) noexcept {
                                    render_expression(expression, item_prefix, item_last);
                                },
                            },
                            step.value
                        );
                    }
                );
            },
        },
        header.value
    );
}

auto ASTDumper::render_statement(
    ASTStmtID statement_id,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    const auto& statement = ast.statement(statement_id);
    std::visit(
        Overloaded {
            [&](const ASTVariableDecl& declaration) noexcept {
                render_variable_declaration(declaration, prefix, is_last, statement.span);
            },
            [&](const ASTAssignment& assignment) noexcept {
                render_assignment(assignment, prefix, is_last, statement.span);
            },
            [&](const ASTUpdate& update) noexcept {
                render_update(update, prefix, is_last, statement.span);
            },
            [&](const ASTExprStatement& expression) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("ExpressionStatement {}", format_dump_span(statement.span))
                );
                render_expression(
                    expression.expression,
                    child_prefix(prefix, is_last),
                    true,
                    "expression "
                );
            },
            [&](const ASTControlTransfer& transfer) noexcept {
                render_control_transfer(transfer, prefix, is_last, statement.span);
            },
            [&](const ASTWhileStmt& loop) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("WhileStatement {}", format_dump_span(statement.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_span_field(nested_prefix, false, "while", loop.keyword_span);
                render_expression(loop.condition, nested_prefix, false, "condition ");
                render_ordinary_block(loop.body, nested_prefix, true, "body ");
            },
            [&](const ASTForStmt& loop) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("ForStatement {}", format_dump_span(statement.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_span_field(nested_prefix, false, "for", loop.keyword_span);
                render_for_header(loop.header, nested_prefix, false);
                render_ordinary_block(loop.body, nested_prefix, true, "body ");
            },
            [&](const ASTIfForm& form) noexcept { render_if_form(form, prefix, is_last); },
            [&](const ASTMatchForm& form) noexcept { render_match_form(form, prefix, is_last); },
            [&](const ASTTryForm& form) noexcept { render_try_form(form, prefix, is_last); },
        },
        statement.value
    );
}
