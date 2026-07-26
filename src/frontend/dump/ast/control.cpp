module carven:frontend.dump.ast.control.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.type;
import :frontend.dump.ast;
import :support.visit;
import std;

auto ASTDumper::render_variable_declaration(
    const ASTVariableDecl& declaration,
    std::string_view prefix,
    bool is_last,
    Span outer_span
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format("VariableDeclarationHead {}", format_dump_span(outer_span))
    );
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, false, "keyword", declaration.keyword_span);
    render_span_field(
        nested,
        false,
        std::holds_alternative<ASTNamedBindingTarget>(declaration.target) ? "name" : "discard",
        binding_target_span(declaration.target)
    );
    if (declaration.type) {
        render_type(*declaration.type, nested, false, "type ");
    } else {
        append_line(nested, false, "type <absent>");
    }
    if (declaration.initializer) {
        render_expression(*declaration.initializer, nested, true, "initializer ");
    } else {
        append_line(nested, true, "initializer <absent>");
    }
}

auto ASTDumper::render_assignment(
    const ASTAssignment& value,
    std::string_view prefix,
    bool is_last,
    Span outer_span
) noexcept -> void {
    append_line(prefix, is_last, std::format("AssignmentForm {}", format_dump_span(outer_span)));
    const auto nested = child_prefix(prefix, is_last);
    render_expression(value.target, nested, false, "target ");
    render_span_field(nested, false, "operator", value.operator_span);
    render_expression(value.value, nested, true, "value ");
}

auto ASTDumper::render_update(
    const ASTUpdate& value,
    std::string_view prefix,
    bool is_last,
    Span outer_span
) noexcept -> void {
    append_line(prefix, is_last, std::format("UpdateForm {}", format_dump_span(outer_span)));
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, false, "operator", value.operator_span);
    render_expression(value.target, nested, true, "target ");
}

auto ASTDumper::render_control_transfer(
    const ASTControlTransfer& value,
    std::string_view prefix,
    bool is_last,
    Span outer_span
) noexcept -> void {
    append_line(
        prefix,
        is_last,
        std::format("ControlTransferForm {}", format_dump_span(outer_span))
    );
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, !value.value.has_value(), "keyword", value.keyword_span);
    if (value.value) {
        render_expression(*value.value, nested, true, "value ");
    }
}

auto ASTDumper::render_throw_clause(
    const std::optional<ASTThrowClause>& clause,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    if (!clause.has_value()) {
        append_line(prefix, is_last, "throw <absent>");
        return;
    }
    append_line(prefix, is_last, std::format("ThrowClause {}", format_dump_span(clause->span)));
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, false, "keyword", clause->keyword_span);
    render_list(
        nested,
        true,
        "failures",
        clause->failures,
        [&](ASTTypeID type, std::string_view item_prefix, bool item_last) noexcept {
            render_type(type, item_prefix, item_last);
        }
    );
}

auto ASTDumper::render_branch_block(
    ASTBranchBlockID id,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    const auto& block = ast.branch_block(id);
    append_line(
        prefix,
        is_last,
        std::format("{}BranchBlock {}", field, format_dump_span(block.span))
    );
    const auto nested = child_prefix(prefix, is_last);
    render_list(
        nested,
        false,
        "statements",
        block.statements,
        [&](ASTStmtID statement, std::string_view item_prefix, bool item_last) noexcept {
            render_statement(statement, item_prefix, item_last);
        }
    );
    if (block.result) {
        render_expression(*block.result, nested, true, "result ");
    } else {
        append_line(nested, true, "result <absent>");
    }
}

auto ASTDumper::render_ordinary_block(
    ASTBlockID id,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    const auto& block = ast.block(id);
    append_line(
        prefix,
        is_last,
        std::format("{}OrdinaryBlock {}", field, format_dump_span(block.span))
    );
    render_list(
        child_prefix(prefix, is_last),
        true,
        "statements",
        block.statements,
        [&](ASTStmtID statement, std::string_view item_prefix, bool item_last) noexcept {
            render_statement(statement, item_prefix, item_last);
        }
    );
}

auto ASTDumper::render_pattern(ASTPatternID id, std::string_view prefix, bool is_last) noexcept
    -> void {
    const auto& pattern = ast.pattern(id);
    const auto render_name = [&](const ASTQualifiedName& name,
                                 std::string_view item_prefix,
                                 bool item_last,
                                 std::string_view field = {}) noexcept {
        append_line(
            item_prefix,
            item_last,
            std::format("{}QualifiedName {}", field, format_dump_span(name.span))
        );
        render_list(
            child_prefix(item_prefix, item_last),
            true,
            "components",
            name.components,
            [&](Span component, std::string_view name_prefix, bool name_last) noexcept {
                render_span_field(name_prefix, name_last, "name", component);
            }
        );
    };
    std::visit(
        Overloaded {
            [&](const ASTWildcardPattern& wildcard) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("WildcardPattern {}", format_dump_span(pattern.span))
                );
                render_span_field(
                    child_prefix(prefix, is_last),
                    true,
                    "underscore",
                    wildcard.underscore_span
                );
            },
            [&](const ASTLiteral& literal) noexcept { render_literal(literal, prefix, is_last); },
            [&](const ASTNegativeNumberPattern& negative) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("NegativeNumberPattern {}", format_dump_span(pattern.span))
                );
                const auto nested = child_prefix(prefix, is_last);
                render_span_field(nested, false, "minus", negative.minus_span);
                render_numeric_literal(negative.number_span, negative.value, nested, true);
            },
            [&](const ASTBindingPattern& binding) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("BindingPattern {}", format_dump_span(pattern.span))
                );
                render_span_field(child_prefix(prefix, is_last), true, "name", binding.name_span);
            },
            [&](const ASTConstraintPattern& constraint) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("ConstraintPattern {}", format_dump_span(pattern.span))
                );
                const auto nested = child_prefix(prefix, is_last);
                render_span_field(nested, false, "is", constraint.is_span);
                std::visit(
                    Overloaded {
                        [&](const ASTQualifiedName& name) noexcept {
                            render_name(name, nested, true, "operand ");
                        },
                        [&](const ASTArrayType& array) noexcept {
                            append_line(
                                nested,
                                true,
                                std::format(
                                    "operand ArrayType {}",
                                    format_dump_span(constraint.operand.span)
                                )
                            );
                            const auto operand = child_prefix(nested, true);
                            render_type(array.element_type, operand, false, "element ");
                            render_expression(array.extent, operand, true, "extent ");
                        },
                    },
                    constraint.operand.value
                );
            },
            [&](const ASTCasePattern& case_pattern) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("CasePattern {}", format_dump_span(pattern.span))
                );
                const auto nested = child_prefix(prefix, is_last);
                std::visit(
                    Overloaded {
                        [&](const ASTContextualCaseQualifier& qualifier) noexcept {
                            render_span_field(
                                nested,
                                false,
                                "contextual qualifier",
                                qualifier.dot_span
                            );
                        },
                        [&](const ASTQualifiedCaseQualifier& qualifier) noexcept {
                            render_span_field(nested, false, "qualifier", qualifier.span);
                            render_list(
                                nested,
                                false,
                                "qualifier_components",
                                qualifier.components,
                                [&](Span component,
                                    std::string_view name_prefix,
                                    bool name_last) noexcept {
                                    render_span_field(name_prefix, name_last, "name", component);
                                }
                            );
                            render_span_field(nested, false, "separator", qualifier.separator_span);
                        },
                    },
                    case_pattern.qualifier
                );
                render_span_field(nested, false, "name", case_pattern.name_span);
                if (case_pattern.payload.has_value()) {
                    render_span_field(
                        nested,
                        false,
                        "left_parenthesis",
                        case_pattern.payload->left_parenthesis_span
                    );
                    render_list(
                        nested,
                        false,
                        "payload",
                        case_pattern.payload->patterns,
                        [&](ASTPatternID payload,
                            std::string_view item_prefix,
                            bool item_last) noexcept {
                            render_pattern(payload, item_prefix, item_last);
                        }
                    );
                    render_span_field(
                        nested,
                        true,
                        "right_parenthesis",
                        case_pattern.payload->right_parenthesis_span
                    );
                }
            },
            [&](const ASTOrPattern& alternatives) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("OrPattern {}", format_dump_span(pattern.span))
                );
                const auto nested = child_prefix(prefix, is_last);
                render_list(
                    nested,
                    false,
                    "alternatives",
                    alternatives.alternatives,
                    [&](ASTPatternID alternative,
                        std::string_view item_prefix,
                        bool item_last) noexcept {
                        render_pattern(alternative, item_prefix, item_last);
                    }
                );
                render_list(
                    nested,
                    true,
                    "pipes",
                    alternatives.pipe_spans,
                    [&](Span pipe, std::string_view item_prefix, bool item_last) noexcept {
                        render_span_field(item_prefix, item_last, "pipe", pipe);
                    }
                );
            },
        },
        pattern.value
    );
}

auto ASTDumper::render_if_form(
    const ASTIfForm& form,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}IfForm {}", field, format_dump_span(form.span)));
    const auto nested = child_prefix(prefix, is_last);
    for (auto index = 0uz; index < form.branches.size(); ++index) {
        const auto& branch = form.branches[index];
        if (index == 0) {
            render_span_field(nested, false, "if", branch.keyword_span);
            render_expression(branch.condition, nested, false, "condition ");
            render_branch_block(branch.body, nested, false, "then ");
        } else {
            append_line(
                nested,
                false,
                std::format(
                    "else_if Branch {}",
                    format_dump_span(ast.branch_block(branch.body).span)
                )
            );
            const auto branch_prefix = child_prefix(nested, false);
            render_span_field(branch_prefix, false, "if", branch.keyword_span);
            render_expression(branch.condition, branch_prefix, false, "condition ");
            render_branch_block(branch.body, branch_prefix, true, "then ");
        }
    }
    if (form.else_branch) {
        render_branch_block(*form.else_branch, nested, true, "else_branch ");
    } else {
        append_line(nested, true, "else_branch <absent>");
    }
}

auto ASTDumper::render_match_form(
    const ASTMatchForm& form,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}MatchForm {}", field, format_dump_span(form.span)));
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, false, "match", form.keyword_span);
    render_expression(form.subject, nested, false, "subject ");
    render_list(
        nested,
        true,
        "arms",
        form.arms,
        [&](const ASTMatchArm& arm, std::string_view item_prefix, bool item_last) noexcept {
            append_line(
                item_prefix,
                item_last,
                std::format("MatchArm {}", format_dump_span(arm.span))
            );
            const auto arm_prefix = child_prefix(item_prefix, item_last);
            render_pattern(arm.pattern, arm_prefix, false);
            if (arm.guard.has_value()) {
                render_span_field(arm_prefix, false, "if", arm.guard->keyword_span);
                render_expression(arm.guard->expression, arm_prefix, false, "guard ");
            } else {
                append_line(arm_prefix, false, "guard <absent>");
            }
            render_span_field(arm_prefix, false, "arrow", arm.arrow_span);
            std::visit(
                Overloaded {
                    [&](ASTExprID expression) noexcept {
                        render_expression(expression, arm_prefix, true, "body ");
                    },
                    [&](const ASTControlTransfer& transfer) noexcept {
                        render_control_transfer(transfer, arm_prefix, true, arm.body.span);
                    },
                    [&](ASTBranchBlockID block) noexcept {
                        render_branch_block(block, arm_prefix, true, "body ");
                    },
                },
                arm.body.value
            );
        }
    );
}

auto ASTDumper::render_try_form(
    const ASTTryForm& form,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}TryForm {}", field, format_dump_span(form.span)));
    const auto nested = child_prefix(prefix, is_last);
    render_span_field(nested, false, "try", form.try_span);
    render_branch_block(form.body, nested, false, "body ");
    render_span_field(nested, false, "catch", form.catch_span);
    render_list(
        nested,
        true,
        "arms",
        form.arms,
        [&](const ASTCatchArm& arm, std::string_view arm_prefix, bool arm_last) noexcept {
            append_line(
                arm_prefix,
                arm_last,
                std::format("CatchArm {}", format_dump_span(arm.span))
            );
            const auto contents = child_prefix(arm_prefix, arm_last);
            render_list(
                contents,
                false,
                "alternatives",
                arm.pattern.alternatives,
                [&](const ASTCatchPatternAtom& atom,
                    std::string_view atom_prefix,
                    bool atom_last) noexcept {
                    std::visit(
                        Overloaded {
                            [&](const ASTCatchWildcardPattern& wildcard) noexcept {
                                render_span_field(
                                    atom_prefix,
                                    atom_last,
                                    "wildcard",
                                    wildcard.underscore_span
                                );
                            },
                            [&](const ASTCatchTypedPattern& typed) noexcept {
                                append_line(atom_prefix, atom_last, "TypedCatchPattern");
                                const auto typed_prefix = child_prefix(atom_prefix, atom_last);
                                render_type(typed.type, typed_prefix, false, "type ");
                                render_pattern(typed.inner, typed_prefix, true);
                            },
                        },
                        atom.value
                    );
                }
            );
            if (arm.guard.has_value()) {
                render_span_field(contents, false, "if", arm.guard->keyword_span);
                render_expression(arm.guard->expression, contents, false, "guard ");
            } else {
                append_line(contents, false, "guard <absent>");
            }
            render_span_field(contents, false, "arrow", arm.arrow_span);
            std::visit(
                Overloaded {
                    [&](ASTExprID expression) noexcept {
                        render_expression(expression, contents, true, "body ");
                    },
                    [&](const ASTControlTransfer& transfer) noexcept {
                        render_control_transfer(transfer, contents, true, arm.body.span);
                    },
                    [&](ASTBranchBlockID block) noexcept {
                        render_branch_block(block, contents, true, "body ");
                    },
                },
                arm.body.value
            );
        }
    );
}
