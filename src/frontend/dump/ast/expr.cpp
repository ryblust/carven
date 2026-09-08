module carven:frontend.dump.ast.expr.impl;

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

auto ASTDumper::render_expression(
    ASTExprID expression_id,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    const auto& expression = ast.expression(expression_id);
    std::visit(
        [&](const auto& value) noexcept {
            render_expression(value, expression_id, prefix, is_last, field);
        },
        expression.value
    );
}

auto ASTDumper::render_expression(
    const ASTLiteral& literal,
    ASTExprID,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    render_literal(literal, prefix, is_last, field);
}

auto ASTDumper::render_expression(
    const ASTNameExpr& name,
    ASTExprID,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format("{}NameExpression {}", field, source_label(name.name_span))
    );
}

auto ASTDumper::render_expression(
    const ASTCppNameExpr& name,
    ASTExprID,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}CppNameExpression", field));
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, false, "global_root", name.global_root);
    render_list(
        nested,
        true,
        "components",
        name.components,
        [&](Span component, std::string_view item_prefix, bool item_last) noexcept {
            render_span_field(item_prefix, item_last, "name", component);
        }
    );
}

auto ASTDumper::render_expression(
    const ASTContextualCaseExpr& contextual,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}ContextualCaseExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_span_field(nested_prefix, false, "dot", contextual.dot_span);
    render_span_field(nested_prefix, true, "name", contextual.name_span);
}

auto ASTDumper::render_expression(
    const ASTGroupExpr& group,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}GroupExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    render_expression(group.expression, child_prefix(prefix, is_last), true, "expression ");
}

auto ASTDumper::render_expression(
    const ASTAccessExpr& access,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    const auto* mode = access.mode == ASTAccessMode::Write ? "Write" : "Take";
    append_line(
        prefix,
        is_last,
        std::format(
            "{}AccessExpression {} {}",
            field,
            mode,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_span_field(nested_prefix, false, "marker", access.marker_span);
    render_expression(access.operand_id, nested_prefix, true, "operand ");
}

auto ASTDumper::render_expression(
    const ASTArrayExpr& array,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}ArrayExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    render_list(
        child_prefix(prefix, is_last),
        true,
        "elements",
        array.element_ids,
        [&](ASTExprID element, std::string_view item_prefix, bool item_last) noexcept {
            render_expression(element, item_prefix, item_last);
        }
    );
}

auto ASTDumper::render_expression(
    const ASTConstructionExpr& construction,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}ConstructionExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_construction_type(construction.type, nested_prefix, false);
    std::visit(
        Overloaded {
            [&](std::monostate) noexcept {
                append_line(nested_prefix, true, "initializer <absent>");
            },
            [&](const ASTPositionalInitializerList& list) noexcept {
                render_list(
                    nested_prefix,
                    true,
                    "initializer positional",
                    list.values,
                    [&](ASTExprID value, std::string_view item_prefix, bool item_last) noexcept {
                        render_expression(value, item_prefix, item_last);
                    }
                );
            },
            [&](const ASTFieldInitializerList& list) noexcept {
                render_list(
                    nested_prefix,
                    true,
                    "initializer fields",
                    list.fields,
                    [&](const ASTFieldInitializer& value,
                        std::string_view item_prefix,
                        bool item_last) noexcept {
                        append_line(
                            item_prefix,
                            item_last,
                            std::format("FieldInitializer {}", format_dump_span(value.span))
                        );
                        const auto value_prefix = child_prefix(item_prefix, item_last);
                        render_span_field(value_prefix, false, "name", value.name_span);
                        render_expression(value.value, value_prefix, true, "value ");
                    }
                );
            },
        },
        construction.initializer.value
    );
}

auto ASTDumper::render_expression(
    const ASTPrefixExpr& unary,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}PrefixExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_span_field(nested_prefix, false, "operator", unary.operator_span);
    render_expression(unary.operand_id, nested_prefix, true, "operand ");
}

auto ASTDumper::render_expression(
    const ASTBinaryExpr& binary,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}BinaryExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_expression(binary.left, nested_prefix, false, "left ");
    render_span_field(nested_prefix, false, "operator", binary.operator_span);
    render_expression(binary.right, nested_prefix, true, "right ");
}

auto ASTDumper::render_expression(
    const ASTCastExpr& cast,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format("{}CastExpression {}", field, format_dump_span(ast.expression(expression).span))
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_expression(cast.operand_id, nested_prefix, false, "operand ");
    render_span_field(nested_prefix, false, "operator", cast.operator_span);
    render_type(cast.target_type, nested_prefix, true, "target ");
}

auto ASTDumper::render_expression(
    const ASTCallExpr& call,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format("{}CallExpression {}", field, format_dump_span(ast.expression(expression).span))
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_expression(call.callee, nested_prefix, false, "callee ");
    render_list(
        nested_prefix,
        true,
        "arguments",
        call.arguments,
        [&](const ASTCallArgument& argument,
            std::string_view item_prefix,
            bool item_last) noexcept {
            render_expression(argument.expression, item_prefix, item_last);
        }
    );
}

auto ASTDumper::render_expression(
    const ASTIndexExpr& index,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}IndexExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_expression(index.operand_id, nested_prefix, false, "operand ");
    render_expression(index.index, nested_prefix, true, "index ");
}

auto ASTDumper::render_expression(
    const ASTMemberExpr& member,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}MemberExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    render_expression(member.operand_id, nested_prefix, false, "operand ");
    render_span_field(nested_prefix, false, "operator", member.operator_span);
    render_span_field(nested_prefix, true, "name", member.name_span);
}

auto ASTDumper::render_expression(
    const ASTLambdaExpr& lambda,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}LambdaExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested = child_prefix(prefix, is_last);
    render_list(
        nested,
        false,
        "captures",
        lambda.captures,
        [&](const ASTLambdaCapture& capture,
            std::string_view item_prefix,
            bool item_last) noexcept {
            append_line(
                item_prefix,
                item_last,
                std::format(
                    "Capture {} {}",
                    capture.write_marker.has_value() ? "Write" : "Value",
                    format_dump_span(capture.span)
                )
            );
            render_span_field(
                child_prefix(item_prefix, item_last),
                true,
                "name",
                capture.name_span
            );
        }
    );
    render_list(
        nested,
        false,
        "parameters",
        lambda.parameters,
        [&](const ASTFunctionParameter& parameter,
            std::string_view item_prefix,
            bool item_last) noexcept {
            append_line(
                item_prefix,
                item_last,
                std::format("LambdaParameter {}", format_dump_span(parameter.span))
            );
            const auto parameter_prefix = child_prefix(item_prefix, item_last);
            if (parameter.access.marker.has_value()) {
                render_span_field(
                    parameter_prefix,
                    false,
                    "access marker",
                    *parameter.access.marker
                );
            } else {
                append_line(parameter_prefix, false, "access Read");
            }
            render_span_field(
                parameter_prefix,
                !parameter.type.has_value(),
                "name",
                binding_target_span(parameter.target)
            );
            if (parameter.type.has_value()) {
                render_type(*parameter.type, parameter_prefix, true, "type ");
            }
        }
    );
    if (lambda.result_type.has_value()) {
        render_type(*lambda.result_type, nested, false, "result ");
    } else {
        append_line(nested, false, "result <absent>");
    }
    render_throw_clause(lambda.throw_clause, nested, false);
    render_callable_body(lambda.body, nested, true, "body ");
}

auto ASTDumper::render_expression(
    const ASTPropagationExpr& propagation,
    ASTExprID expression,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format(
            "{}PropagationExpression {}",
            field,
            format_dump_span(ast.expression(expression).span)
        )
    );
    const auto nested = child_prefix(prefix, is_last);
    render_expression(propagation.operand_id, nested, false, "operand ");
    render_span_field(nested, true, "operator", propagation.operator_span);
}

auto ASTDumper::render_expression(
    const ASTIfForm& form,
    ASTExprID,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    render_if_form(form, prefix, is_last, field);
}

auto ASTDumper::render_expression(
    const ASTMatchForm& form,
    ASTExprID,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    render_match_form(form, prefix, is_last, field);
}

auto ASTDumper::render_expression(
    const ASTTryForm& form,
    ASTExprID,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    render_try_form(form, prefix, is_last, field);
}
