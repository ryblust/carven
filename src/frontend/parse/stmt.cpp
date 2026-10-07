module carven:frontend.parse.stmt.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.parse.builder;
import :frontend.parse.parser;
import :source.text;
import std;

auto Parser::parse_ordinary_block() noexcept -> std::optional<ASTBlockID> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    const auto left = expect(TokenKind::LeftBrace, "expected '{'");
    auto statements = std::vector<ASTStmtID> {};
    while (!failed && !check(TokenKind::RightBrace)) {
        auto statement = parse_statement();
        if (!statement) {
            return std::nullopt;
        }
        statements.push_back(*statement);
    }
    const auto right = expect(TokenKind::RightBrace, "expected '}' after block");
    if (failed) {
        return std::nullopt;
    }
    return builder.append_block(
        ASTBlock {
            .span = join(left.span, right.span),
            .statements = std::move(statements),
        }
    );
}

auto Parser::parse_branch_block() noexcept -> std::optional<ASTBranchBlockID> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    const auto left = expect(TokenKind::LeftBrace, "expected '{'");
    auto statements = std::vector<ASTStmtID> {};
    auto result_expression = std::optional<ASTExprID> {};
    while (!failed && !check(TokenKind::RightBrace)) {
        if (starts_unambiguous_statement()) {
            auto statement = parse_statement();
            if (!statement) {
                return std::nullopt;
            }
            statements.push_back(*statement);
            continue;
        }
        const auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        if (const auto assignment = finish_assignment(*expression)) {
            const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after assignment");
            statements.push_back(builder.append_statement(
                ASTStmt {
                    .span = join(assignment->span, semicolon.span),
                    .value = *assignment,
                }
            ));
            continue;
        }

        if (const auto semicolon = match(TokenKind::Semicolon)) {
            statements.push_back(builder.append_statement(
                ASTStmt {
                    .span = join(builder.expression(*expression).span, semicolon->span),
                    .value = ASTExprStatement {.expression = *expression},
                }
            ));
            continue;
        }
        result_expression = *expression;
        break;
    }
    const auto right = expect(TokenKind::RightBrace, "expected '}' after branch block");
    if (failed) {
        return std::nullopt;
    }
    return builder.append_branch_block(
        ASTBranchBlock {
            .span = join(left.span, right.span),
            .statements = std::move(statements),
            .result = result_expression,
        }
    );
}

auto Parser::starts_unambiguous_statement() const noexcept -> bool {
    return check(TokenKind::Async)
        || check(TokenKind::Let)
        || check(TokenKind::Var)
        || check(TokenKind::Const)
        || check(TokenKind::Return)
        || check(TokenKind::Break)
        || check(TokenKind::Continue)
        || check(TokenKind::Throw)
        || check(TokenKind::Rethrow)
        || check(TokenKind::If)
        || check(TokenKind::Match)
        || check(TokenKind::Try)
        || check(TokenKind::While)
        || check(TokenKind::For)
        || check(TokenKind::PlusPlus)
        || check(TokenKind::MinusMinus);
}

auto Parser::parse_statement() noexcept -> std::optional<ASTStmtID> {
    if (check(TokenKind::Const)
        && (check_next(TokenKind::LeftBrace) || check_next(TokenKind::StringLiteral))) {
        const auto keyword = consume();
        auto label = parse_block_label();
        const auto body = parse_ordinary_block();
        if (!body) {
            return std::nullopt;
        }
        return builder.append_statement({
            .span = join(keyword.span, builder.block(*body).span),
            .value = ASTConstBlock {
                .keyword_span = keyword.span,
                .label = std::move(label),
                .body = *body,
            },
        });
    }
    if (check_optionally_const(TokenKind::For)) {
        return parse_for_statement();
    }
    if (check_optionally_const(TokenKind::If)) {
        auto form = parse_if_form();
        if (!form) {
            return std::nullopt;
        }
        return builder.append_statement({
            .span = form->span,
            .value = std::move(*form),
        });
    }
    if (check(TokenKind::Async)
        || check(TokenKind::Let)
        || check(TokenKind::Var)
        || check(TokenKind::Const)) {
        const auto declaration = parse_variable_declaration_head();
        if (!declaration) {
            return std::nullopt;
        }
        const auto semicolon =
            expect(TokenKind::Semicolon, "expected ';' after variable declaration");
        return builder.append_statement({
            .span = join(declaration->span, semicolon.span),
            .value = *declaration,
        });
    }
    if (check(TokenKind::Return)
        || check(TokenKind::Break)
        || check(TokenKind::Continue)
        || check(TokenKind::Throw)
        || check(TokenKind::Rethrow)) {
        const auto transfer = parse_control_transfer(true);
        if (!transfer) {
            return std::nullopt;
        }
        return builder.append_statement({
            .span = transfer->span,
            .value = *transfer,
        });
    }
    if (check(TokenKind::While)) {
        return parse_while_statement();
    }
    if (check(TokenKind::Match)) {
        auto form = parse_match_form();
        if (!form) {
            return std::nullopt;
        }
        return builder.append_statement({
            .span = form->span,
            .value = std::move(*form),
        });
    }
    if (check(TokenKind::Try)) {
        auto form = parse_try_form();
        if (!form) {
            return std::nullopt;
        }
        return builder.append_statement({
            .span = form->span,
            .value = std::move(*form),
        });
    }
    if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
        const auto update = parse_update();
        if (!update) {
            return std::nullopt;
        }
        const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after update");
        return builder.append_statement({
            .span = join(update->span, semicolon.span),
            .value = *update,
        });
    }
    const auto expression = parse_expression();
    if (!expression) {
        return std::nullopt;
    }
    if (const auto assignment = finish_assignment(*expression)) {
        const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after assignment");
        return builder.append_statement({
            .span = join(assignment->span, semicolon.span),
            .value = *assignment,
        });
    }

    const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after expression statement");
    if (failed) {
        return std::nullopt;
    }
    return builder.append_statement({
        .span = join(builder.expression(*expression).span, semicolon.span),
        .value = ASTExprStatement {.expression = *expression},
    });
}

auto Parser::parse_variable_declaration_head() noexcept -> std::optional<ASTVariableDecl> {
    auto async_span = std::optional<Span>();
    if (const auto keyword = match(TokenKind::Async)) {
        async_span = keyword->span;
        if (!check(TokenKind::Let)) {
            fail_here("expected let after async");
            return std::nullopt;
        }
    }
    const auto keyword = consume();
    const auto kind = keyword.kind == TokenKind::Var ? ASTBindingKind::Var
        : keyword.kind == TokenKind::Const           ? ASTBindingKind::Const
                                                     : ASTBindingKind::Let;
    const auto name = expect(TokenKind::Identifier, "expected binding name");
    auto type = std::optional<ASTTypeID> {};
    if (match(TokenKind::Colon)) {
        type = parse_type();
    }
    expect(TokenKind::Equal, "expected '=' in variable declaration");
    const auto initializer = parse_expression();
    if (!initializer) {
        return std::nullopt;
    }
    return ASTVariableDecl {
        .span = join(async_span.value_or(keyword.span), builder.expression(*initializer).span),
        .kind = kind,
        .keyword_span = keyword.span,
        .async_span = async_span,
        .target = slice(source, name.span) == "_" ? ASTBindingTarget {ASTDiscardBindingTarget {
                                                        .underscore_span = name.span,
                                                    }}
                                                  : ASTBindingTarget {ASTNamedBindingTarget {
                                                        .name_span = name.span,
                                                    }},
        .type = type,
        .initializer = *initializer,
    };
}

auto Parser::assignment_operator(TokenKind kind) noexcept -> std::optional<ASTAssignmentOperator> {
    switch (kind) {
        case TokenKind::Equal:           return ASTAssignmentOperator::Assign;
        case TokenKind::PlusEqual:       return ASTAssignmentOperator::Add;
        case TokenKind::MinusEqual:      return ASTAssignmentOperator::Subtract;
        case TokenKind::StarEqual:       return ASTAssignmentOperator::Multiply;
        case TokenKind::SlashEqual:      return ASTAssignmentOperator::Divide;
        case TokenKind::PercentEqual:    return ASTAssignmentOperator::Remainder;
        case TokenKind::AmpersandEqual:  return ASTAssignmentOperator::BitwiseAnd;
        case TokenKind::PipeEqual:       return ASTAssignmentOperator::BitwiseOr;
        case TokenKind::CaretEqual:      return ASTAssignmentOperator::BitwiseXor;
        case TokenKind::LeftShiftEqual:  return ASTAssignmentOperator::LeftShift;
        case TokenKind::RightShiftEqual: return ASTAssignmentOperator::RightShift;
        default:                         return std::nullopt;
    }
}

auto Parser::finish_assignment(ASTExprID target) noexcept -> std::optional<ASTAssignment> {
    const auto op = assignment_operator(current().kind);
    if (!op) {
        return std::nullopt;
    }
    const auto operation = consume();
    const auto value = parse_expression();
    if (!value) {
        return std::nullopt;
    }
    return ASTAssignment {
        .span = join(builder.expression(target).span, builder.expression(*value).span),
        .target = target,
        .op = *op,
        .operator_span = operation.span,
        .value = *value,
    };
}

auto Parser::parse_update() noexcept -> std::optional<ASTUpdate> {
    const auto operation = consume();
    const auto target = parse_prefix_expression();
    if (!target) {
        return std::nullopt;
    }
    return ASTUpdate {
        .span = join(operation.span, builder.expression(*target).span),
        .op = operation.kind == TokenKind::PlusPlus ? ASTUpdateOperator::Increment
                                                    : ASTUpdateOperator::Decrement,
        .operator_span = operation.span,
        .target = *target,
    };
}

auto Parser::parse_control_transfer(bool with_semicolon) noexcept
    -> std::optional<ASTControlTransfer> {
    const auto keyword = consume();
    const auto kind = keyword.kind == TokenKind::Break ? ASTControlTransferKind::Break
        : keyword.kind == TokenKind::Continue          ? ASTControlTransferKind::Continue
        : keyword.kind == TokenKind::Throw             ? ASTControlTransferKind::Throw
        : keyword.kind == TokenKind::Rethrow           ? ASTControlTransferKind::Rethrow
                                                       : ASTControlTransferKind::Return;

    auto value = std::optional<ASTExprID> {};
    if (kind == ASTControlTransferKind::Throw) {
        value = parse_expression();
    } else if (kind == ASTControlTransferKind::Return
               && !check(TokenKind::Semicolon)
               && !check(TokenKind::Comma)
               && !check(TokenKind::RightBrace)) {
        value = parse_expression();
    }
    if (failed) {
        return std::nullopt;
    }

    auto end = value.has_value() ? builder.expression(*value).span : keyword.span;
    if (with_semicolon) {
        const auto semicolon = expect(TokenKind::Semicolon, "expected ';' after control transfer");
        end = semicolon.span;
    }
    if (failed) {
        return std::nullopt;
    }
    return ASTControlTransfer {
        .span = join(keyword.span, end),
        .kind = kind,
        .keyword_span = keyword.span,
        .value = value,
    };
}

auto Parser::parse_while_statement() noexcept -> std::optional<ASTStmtID> {
    const auto keyword = expect(TokenKind::While, "expected 'while'");
    auto condition = std::optional<ASTExprID> {};
    if (!check(TokenKind::LeftBrace)) {
        condition = parse_expression_before_block();
        if (!condition) {
            return std::nullopt;
        }
    }
    const auto body = parse_ordinary_block();
    if (!body) {
        return std::nullopt;
    }
    return builder.append_statement({
        .span = join(keyword.span, builder.block(*body).span),
        .value = ASTWhileStmt {
            .keyword_span = keyword.span,
            .condition = condition,
            .body = *body,
        },
    });
}

auto Parser::parse_for_statement() noexcept -> std::optional<ASTStmtID> {
    const auto const_keyword = match(TokenKind::Const);
    const auto keyword = expect(TokenKind::For, "expected 'for'");
    const auto const_span =
        const_keyword.transform([](const Token& token) static { return token.span; });
    const auto header = parse_for_header(const_span);
    if (!header) {
        return std::nullopt;
    }
    const auto body = parse_ordinary_block();
    if (!body) {
        return std::nullopt;
    }
    return builder.append_statement({
        .span = join(const_keyword.value_or(keyword).span, builder.block(*body).span),
        .value = ASTForStmt {
            .const_span = const_span,
            .keyword_span = keyword.span,
            .header = *header,
            .body = *body,
        },
    });
}

auto Parser::parse_for_header(std::optional<Span> const_span) noexcept
    -> std::optional<ASTForHeader> {
    const auto start = current().span;
    const auto checkpoint = begin_speculation();
    const auto access = match(TokenKind::Ampersand);
    const auto name = expect(TokenKind::Identifier, "expected range binding");
    auto type = std::optional<ASTTypeID> {};
    if (!failed && match(TokenKind::Colon)) {
        type = parse_type();
    }
    const auto range_form = !failed && check(TokenKind::In);
    finish_speculation(checkpoint, range_form, !const_span);
    if (range_form) {
        consume();
        const auto begin = parse_expression_before_block();
        if (!begin) {
            return std::nullopt;
        }
        const auto iterable = *begin;
        const auto end_span = builder.expression(*begin).span;
        return ASTForHeader {
            .span = join(start, end_span),
            .value = ASTRangeForHeader {
                .write_marker =
                    access.transform([](const Token& token) static { return token.span; }),
                .target = slice(source, name.span) == "_"
                    ? ASTBindingTarget {ASTDiscardBindingTarget {
                          .underscore_span = name.span,
                      }}
                    : ASTBindingTarget {ASTNamedBindingTarget {
                          .name_span = name.span,
                      }},
                .type = type,
                .iterable = iterable,
            },
        };
    }

    if (const_span) {
        fail("'const for' requires a range header", *const_span);
        return std::nullopt;
    }
    auto initializer = ASTForInitializer {
        .span = start,
        .value = std::monostate {},
    };
    if (!check(TokenKind::Semicolon)) {
        if (check(TokenKind::Async)
            || check(TokenKind::Let)
            || check(TokenKind::Var)
            || check(TokenKind::Const)) {
            const auto declaration = parse_variable_declaration_head();
            if (!declaration) {
                return std::nullopt;
            }
            initializer = {.span = declaration->span, .value = *declaration};
        } else {
            const auto expression = parse_expression();
            if (!expression) {
                return std::nullopt;
            }
            if (const auto assignment = finish_assignment(*expression)) {
                initializer = {
                    .span = assignment->span,
                    .value = *assignment,
                };
            } else {
                initializer = {
                    .span = builder.expression(*expression).span,
                    .value = *expression,
                };
            }
        }
    }
    expect(TokenKind::Semicolon, "expected first ';' in C-style for header");

    if (check(TokenKind::Semicolon)) {
        fail_here("C-style for requires a condition; write a loop without one as 'while { }'");
        return std::nullopt;
    }
    const auto condition = parse_expression();
    if (!condition) {
        return std::nullopt;
    }
    expect(TokenKind::Semicolon, "expected second ';' in C-style for header");

    auto steps = std::vector<ASTForStep> {};
    if (!check(TokenKind::LeftBrace)) {
        const auto depth = enter_depth(block_boundary_depth);
        const auto boundary = set_depth(expression_nesting, 0);
        while (!failed) {
            auto step = parse_for_step();
            if (!step) {
                return std::nullopt;
            }
            steps.push_back(*step);
            if (!match(TokenKind::Comma)) {
                break;
            }
            if (check(TokenKind::LeftBrace)) {
                fail_here("expected for step after ','");
                break;
            }
        }
    }
    const auto end = failed ? current().span
        : !steps.empty()    ? steps.back().span
                            : builder.expression(*condition).span;
    if (failed) {
        return std::nullopt;
    }
    return ASTForHeader {
        .span = join(start, end),
        .value = ASTCStyleForHeader {
            .initializer = initializer,
            .condition = *condition,
            .steps = std::move(steps),
        },
    };
}

auto Parser::parse_for_step() noexcept -> std::optional<ASTForStep> {
    if (check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
        const auto update = parse_update();
        if (!update) {
            return std::nullopt;
        }
        return ASTForStep {.span = update->span, .value = *update};
    }
    const auto expression = parse_expression();
    if (!expression) {
        return std::nullopt;
    }
    if (const auto assignment = finish_assignment(*expression)) {
        return ASTForStep {.span = assignment->span, .value = *assignment};
    }
    return ASTForStep {
        .span = builder.expression(*expression).span,
        .value = *expression,
    };
}
