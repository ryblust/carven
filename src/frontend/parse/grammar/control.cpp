module carven:frontend.parse.grammar.control.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.literal;
import :frontend.parse.builder;
import :frontend.parse.context;
import :source.text;
import :support.invariant;
import std;

namespace {

auto ast_literal_value(const TokenLiteralValue& value) noexcept -> ASTLiteralValue {
    return std::visit(
        [](const auto& alternative) static noexcept -> ASTLiteralValue { return alternative; },
        value
    );
}

auto numeric_literal_value(const TokenLiteralValue& value) noexcept -> NumericLiteralValue {
    return std::visit(
        []<typename Value>(const Value& alternative) static noexcept -> NumericLiteralValue {
            if constexpr (std::same_as<Value, IntegerLiteralValue>
                          || std::same_as<Value, FloatingLiteralValue>) {
                return alternative;
            } else {
                invariant_violation("number token does not carry a numeric literal value");
            }
        },
        value
    );
}

auto string_literal_value(const TokenLiteralValue& value) noexcept -> StringLiteralValue {
    const auto* string = std::get_if<StringLiteralValue>(&value);
    if (string == nullptr) {
        invariant_violation("string token does not carry a string literal value");
    }
    return *string;
}

} // namespace

auto Parser::parse_expression_before_block() noexcept -> std::optional<ASTExprID> {
    const auto depth = enter_depth(block_boundary_depth);
    const auto expression = parse_expression();
    return expression;
}

auto Parser::parse_if_form() noexcept -> std::optional<ASTIfForm> {
    const auto first_keyword = expect(TokenKind::If, "expected 'if'");
    auto branches = std::vector<ASTIfForm::Branch> {};
    auto keyword = first_keyword;
    auto final_else = std::optional<ASTBranchBlockID> {};
    auto end = first_keyword.span;

    while (!failed) {
        const auto condition = parse_expression_before_block();
        if (!condition) {
            return std::nullopt;
        }
        const auto body = parse_branch_block();
        if (!body) {
            return std::nullopt;
        }
        end = builder.branch_block(*body).span;
        branches.push_back({
            .keyword_span = keyword.span,
            .condition = *condition,
            .body = *body,
        });
        if (!match(TokenKind::Else)) {
            break;
        }
        if (check(TokenKind::If)) {
            keyword = consume();
            continue;
        }
        final_else = parse_branch_block();
        if (!final_else) {
            return std::nullopt;
        }
        end = builder.branch_block(*final_else).span;
        break;
    }
    return ASTIfForm {
        .span = join(first_keyword.span, end),
        .branches = std::move(branches),
        .else_branch = final_else,
    };
}

auto Parser::parse_match_form() noexcept -> std::optional<ASTMatchForm> {
    const auto keyword = expect(TokenKind::Match, "expected 'match'");
    const auto subject = parse_expression_before_block();
    if (!subject) {
        return std::nullopt;
    }
    expect(TokenKind::LeftBrace, "expected '{' after match subject");
    auto arms = std::vector<ASTMatchArm> {};
    while (!failed && !check(TokenKind::RightBrace)) {
        auto arm = parse_match_arm();
        if (!arm) {
            return std::nullopt;
        }
        arms.push_back(std::move(*arm));
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightBrace)) {
            break;
        }
    }
    const auto right = expect(TokenKind::RightBrace, "expected '}' after match arms");
    if (failed) {
        return std::nullopt;
    }
    return ASTMatchForm {
        .span = join(keyword.span, right.span),
        .keyword_span = keyword.span,
        .subject = *subject,
        .arms = std::move(arms),
    };
}

auto Parser::parse_match_arm() noexcept -> std::optional<ASTMatchArm> {
    const auto start = current().span;
    const auto pattern = parse_pattern();
    if (!pattern) {
        return std::nullopt;
    }
    auto guard = std::optional<ASTGuard> {};
    if (const auto keyword = match(TokenKind::If)) {
        const auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        guard = ASTGuard {
            .keyword_span = keyword->span,
            .expression = *expression,
        };
    }
    const auto arrow = expect(TokenKind::FatArrow, "expected '=>' after pattern");
    const auto body = [&]() noexcept -> std::optional<ASTMatchArmBody> {
        if (check(TokenKind::Return)
            || check(TokenKind::Break)
            || check(TokenKind::Continue)
            || check(TokenKind::Throw)
            || check(TokenKind::Rethrow)) {
            const auto transfer = parse_control_transfer(false);
            if (!transfer) {
                return std::nullopt;
            }
            return ASTMatchArmBody {
                .span = transfer->span,
                .value = *transfer,
            };
        }
        if (check(TokenKind::LeftBrace)) {
            const auto block = parse_branch_block();
            if (!block) {
                return std::nullopt;
            }
            return ASTMatchArmBody {
                .span = builder.branch_block(*block).span,
                .value = *block,
            };
        }
        const auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        return ASTMatchArmBody {
            .span = builder.expression(*expression).span,
            .value = *expression,
        };
    }();
    if (!body || failed) {
        return std::nullopt;
    }
    return ASTMatchArm {
        .span = join(start, body->span),
        .pattern = *pattern,
        .guard = guard,
        .arrow_span = arrow.span,
        .body = *body,
    };
}

auto Parser::parse_try_form() noexcept -> std::optional<ASTTryForm> {
    const auto keyword = expect(TokenKind::Try, "expected 'try'");
    const auto body = parse_branch_block();
    if (!body) {
        return std::nullopt;
    }
    const auto catch_keyword = expect(TokenKind::Catch, "expected 'catch' after try body");
    expect(TokenKind::LeftBrace, "expected '{' after 'catch'");
    auto arms = std::vector<ASTCatchArm> {};
    while (!failed && !check(TokenKind::RightBrace)) {
        auto arm = parse_catch_arm();
        if (!arm) {
            return std::nullopt;
        }
        arms.push_back(std::move(*arm));
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightBrace)) {
            break;
        }
    }
    const auto right = expect(TokenKind::RightBrace, "expected '}' after catch arms");
    if (failed) {
        return std::nullopt;
    }
    return ASTTryForm {
        .span = join(keyword.span, right.span),
        .try_span = keyword.span,
        .body = *body,
        .catch_span = catch_keyword.span,
        .arms = std::move(arms),
    };
}

auto Parser::parse_catch_arm() noexcept -> std::optional<ASTCatchArm> {
    const auto start = current().span;
    auto pattern = parse_catch_pattern();
    if (!pattern) {
        return std::nullopt;
    }
    auto guard = std::optional<ASTGuard> {};
    if (const auto keyword = match(TokenKind::If)) {
        const auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        guard = ASTGuard {
            .keyword_span = keyword->span,
            .expression = *expression,
        };
    }
    const auto arrow = expect(TokenKind::FatArrow, "expected '=>' after catch pattern");
    auto body = [&]() noexcept -> std::optional<ASTMatchArmBody> {
        if (check(TokenKind::Return)
            || check(TokenKind::Break)
            || check(TokenKind::Continue)
            || check(TokenKind::Throw)
            || check(TokenKind::Rethrow)) {
            const auto transfer = parse_control_transfer(false);
            if (!transfer) {
                return std::nullopt;
            }
            return ASTMatchArmBody {
                .span = transfer->span,
                .value = *transfer,
            };
        }
        if (check(TokenKind::LeftBrace)) {
            const auto block = parse_branch_block();
            if (!block) {
                return std::nullopt;
            }
            return ASTMatchArmBody {
                .span = builder.branch_block(*block).span,
                .value = *block,
            };
        }
        const auto expression = parse_expression();
        if (!expression) {
            return std::nullopt;
        }
        return ASTMatchArmBody {
            .span = builder.expression(*expression).span,
            .value = *expression,
        };
    }();
    if (!body || failed) {
        return std::nullopt;
    }
    return ASTCatchArm {
        .span = join(start, body->span),
        .pattern = std::move(*pattern),
        .guard = guard,
        .arrow_span = arrow.span,
        .body = std::move(*body),
    };
}

auto Parser::parse_catch_pattern() noexcept -> std::optional<ASTCatchPattern> {
    auto alternatives = std::vector<ASTCatchPatternAtom> {};
    auto pipes = std::vector<Span> {};
    auto first = parse_catch_pattern_atom();
    if (!first) {
        return std::nullopt;
    }
    alternatives.push_back(std::move(*first));
    while (const auto pipe = match(TokenKind::Pipe)) {
        pipes.push_back(pipe->span);
        auto alternative = parse_catch_pattern_atom();
        if (!alternative) {
            return std::nullopt;
        }
        alternatives.push_back(std::move(*alternative));
    }
    const auto start = alternatives.empty() ? current().span : alternatives.front().span;
    const auto end = alternatives.empty() ? current().span : alternatives.back().span;
    return ASTCatchPattern {
        .span = join(start, end),
        .alternatives = std::move(alternatives),
        .pipe_spans = std::move(pipes),
    };
}

auto Parser::parse_catch_pattern_atom() noexcept -> std::optional<ASTCatchPatternAtom> {
    if (check(TokenKind::Identifier) && slice(source, current().span) == "_") {
        const auto wildcard = consume();
        return ASTCatchPatternAtom {
            .span = wildcard.span,
            .value = ASTCatchWildcardPattern {
                .underscore_span = wildcard.span,
            },
        };
    }
    const auto type = parse_named_type();
    if (!type) {
        return std::nullopt;
    }
    const auto left = expect(TokenKind::LeftParen, "expected '(' after catch error type");
    const auto inner = parse_pattern();
    const auto right = expect(TokenKind::RightParen, "expected ')' after catch inner pattern");
    if (!inner || failed) {
        return std::nullopt;
    }
    const auto start = builder.type(*type).span;
    return ASTCatchPatternAtom {
        .span = join(start, right.span),
        .value = ASTCatchTypedPattern {
            .type = *type,
            .left_parenthesis_span = left.span,
            .inner = *inner,
            .right_parenthesis_span = right.span,
        },
    };
}

auto Parser::parse_pattern() noexcept -> std::optional<ASTPatternID> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    const auto first = parse_primary_pattern();
    if (!first || !check(TokenKind::Pipe)) {
        return first;
    }

    auto alternatives = std::vector<ASTPatternID> {*first};
    auto pipes = std::vector<Span> {};
    while (const auto pipe = match(TokenKind::Pipe)) {
        pipes.push_back(pipe->span);
        auto alternative = parse_primary_pattern();
        if (!alternative) {
            return std::nullopt;
        }
        alternatives.push_back(*alternative);
    }
    const auto end = builder.pattern(alternatives.back()).span;
    return builder.append_pattern(
        ASTPattern {
            .span = join(builder.pattern(*first).span, end),
            .value = ASTOrPattern {
                .alternatives = std::move(alternatives),
                .pipe_spans = std::move(pipes),
            },
        }
    );
}

auto Parser::parse_primary_pattern() noexcept -> std::optional<ASTPatternID> {
    const auto start = current().span;
    if (check(TokenKind::Identifier) && slice(source, current().span) == "_") {
        const auto wildcard = consume();
        return builder.append_pattern(
            ASTPattern {
                .span = wildcard.span,
                .value = ASTWildcardPattern {.underscore_span = wildcard.span},
            }
        );
    }
    if (check(TokenKind::NumberLiteral)
        || check(TokenKind::StringLiteral)
        || check(TokenKind::CharLiteral)
        || check(TokenKind::True)
        || check(TokenKind::False)) {
        auto value = consume_literal();
        return builder.append_pattern(
            ASTPattern {
                .span = value.span,
                .value = std::move(value),
            }
        );
    }
    if (const auto minus = match(TokenKind::Minus)) {
        auto number = expect_numeric_literal("expected number after '-' in pattern");
        if (!number.has_value()) {
            return std::nullopt;
        }
        return builder.append_pattern(
            ASTPattern {
                .span = join(minus->span, number->span),
                .value = ASTNegativeNumberPattern {
                    .minus_span = minus->span,
                    .number_span = number->span,
                    .value = std::move(number->value),
                },
            }
        );
    }
    if (const auto keyword = match(TokenKind::Is)) {
        if (check(TokenKind::Identifier)) {
            const auto name = parse_qualified_name();
            const auto operand_span = name.span;
            return builder.append_pattern(
                ASTPattern {
                    .span = join(keyword->span, operand_span),
                    .value = ASTConstraintPattern {
                        .is_span = keyword->span,
                        .operand = {.span = operand_span, .value = name},
                    },
                }
            );
        }
        if (check(TokenKind::LeftBracket)) {
            const auto type = parse_array_type();
            if (!type) {
                return std::nullopt;
            }
            const auto& parsed = builder.type(*type);
            if (!std::holds_alternative<ASTArrayType>(parsed.value)) {
                fail_here("expected array type after 'is'");
                return std::nullopt;
            }
            return builder.append_pattern(
                ASTPattern {
                    .span = join(keyword->span, parsed.span),
                    .value = ASTConstraintPattern {
                        .is_span = keyword->span,
                        .operand = {
                            .span = parsed.span,
                            .value = std::get<ASTArrayType>(parsed.value),
                        },
                    },
                }
            );
        }
        fail_here("expected qualified name or array type after 'is'");
        return std::nullopt;
    }
    if (const auto dot = match(TokenKind::Dot)) {
        const auto name = expect(TokenKind::Identifier, "expected case name after '.'");
        return finish_case_pattern(
            start,
            ASTContextualCaseQualifier {.dot_span = dot->span},
            name.span
        );
    }
    if (check(TokenKind::Identifier)) {
        auto components = std::vector<Span> {consume().span};
        auto separators = std::vector<Span> {};
        while (const auto separator = match(TokenKind::ColonColon)) {
            separators.push_back(separator->span);
            components.push_back(expect(TokenKind::Identifier, "expected name after '::'").span);
        }
        if (components.size() == 1) {
            return builder.append_pattern(
                ASTPattern {
                    .span = components.front(),
                    .value = ASTBindingPattern {.name_span = components.front()},
                }
            );
        }
        const auto name = components.back();
        components.pop_back();
        return finish_case_pattern(
            start,
            ASTQualifiedCaseQualifier {
                .span = join(components.front(), components.back()),
                .components = std::move(components),
                .separator_span = separators.back(),
            },
            name
        );
    }
    fail_here("expected pattern");
    return std::nullopt;
}

auto Parser::finish_case_pattern(Span start, ASTCaseQualifier qualifier, Span name_span) noexcept
    -> std::optional<ASTPatternID> {
    auto payload = std::optional<ASTCasePayload> {};
    auto end = name_span;
    if (const auto left = match(TokenKind::LeftParen)) {
        auto patterns = std::vector<ASTPatternID> {};
        while (!failed && !check(TokenKind::RightParen)) {
            auto pattern = parse_pattern();
            if (!pattern) {
                return std::nullopt;
            }
            patterns.push_back(*pattern);
            if (!match(TokenKind::Comma)) {
                break;
            }
            if (check(TokenKind::RightParen)) {
                break;
            }
        }
        const auto right = expect(TokenKind::RightParen, "expected ')' after case pattern payload");
        if (failed) {
            return std::nullopt;
        }
        payload = ASTCasePayload {
            .left_parenthesis_span = left->span,
            .patterns = std::move(patterns),
            .right_parenthesis_span = right.span,
        };
        end = right.span;
    }
    return builder.append_pattern(
        ASTPattern {
            .span = join(start, end),
            .value = ASTCasePattern {
                .qualifier = std::move(qualifier),
                .name_span = name_span,
                .payload = std::move(payload),
            },
        }
    );
}

auto Parser::parse_qualified_name() noexcept -> ASTQualifiedName {
    const auto start = expect(TokenKind::Identifier, "expected name");
    auto components = std::vector<Span> {start.span};
    auto end = start.span;
    while (match(TokenKind::ColonColon)) {
        end = expect(TokenKind::Identifier, "expected name after '::'").span;
        components.push_back(end);
    }
    return {
        .span = join(start.span, end),
        .components = std::move(components),
    };
}

auto Parser::consume_literal() noexcept -> ASTLiteral {
    const auto token_index = cursor;
    const auto token = consume();
    if (token.kind == TokenKind::True || token.kind == TokenKind::False) {
        return {
            .span = token.span,
            .value = BooleanLiteralValue {.value = token.kind == TokenKind::True},
        };
    }
    return {
        .span = token.span,
        .value = ast_literal_value(token_buffer->literal_value(token_index)),
    };
}

auto Parser::expect_numeric_literal(std::string_view message) noexcept
    -> std::optional<ParsedNumericLiteral> {
    if (!check(TokenKind::NumberLiteral)) {
        fail_here(message);
        return std::nullopt;
    }
    const auto token_index = cursor;
    const auto token = consume();
    return ParsedNumericLiteral {
        .span = token.span,
        .value = numeric_literal_value(token_buffer->literal_value(token_index)),
    };
}

auto Parser::expect_string_literal(std::string_view message) noexcept
    -> std::optional<ParsedStringLiteral> {
    if (!check(TokenKind::StringLiteral)) {
        fail_here(message);
        return std::nullopt;
    }
    const auto token_index = cursor;
    const auto token = consume();
    return ParsedStringLiteral {
        .span = token.span,
        .value = string_literal_value(token_buffer->literal_value(token_index)),
    };
}
