module carven:frontend.parse.grammar.expr.impl;

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
import :frontend.parse.context;
import :source.text;
import std;

auto Parser::parse_expression() noexcept -> std::optional<ASTExprID> {
    const auto nesting = enter_syntax_nesting();
    if (!nesting) {
        return std::nullopt;
    }
    if (check(TokenKind::Ampersand) || check(TokenKind::AmpersandAmpersand)) {
        const auto marker = consume();
        const auto operand = parse_expression();
        if (!operand) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = join(marker.span, builder.expression(*operand).span),
                .value = ASTAccessExpr {
                    .mode = marker.kind == TokenKind::Ampersand ? ASTAccessMode::Write
                                                                : ASTAccessMode::Take,
                    .marker_span = marker.span,
                    .operand_id = *operand,
                },
            }
        );
    }
    return parse_logical_or();
}

template<typename ParseOperand>
auto Parser::parse_left_associative(
    ParseOperand parse_operand,
    std::span<const std::pair<TokenKind, ASTBinaryOperator>> operators
) noexcept -> std::optional<ASTExprID> {
    auto left = (this->*parse_operand)();
    if (!left) {
        return std::nullopt;
    }
    while (!failed) {
        auto operation = std::optional<ASTBinaryOperator> {};
        for (const auto& [kind, value] : operators) {
            if (check(kind)) {
                operation = value;
                break;
            }
        }
        if (!operation) {
            break;
        }
        const auto token = consume();
        const auto right = (this->*parse_operand)();
        if (!right) {
            return std::nullopt;
        }
        left = builder.append_expression(
            ASTExpr {
                .span = join(builder.expression(*left).span, builder.expression(*right).span),
                .value = ASTBinaryExpr {
                    .left = *left,
                    .op = *operation,
                    .operator_span = token.span,
                    .right = *right,
                },
            }
        );
    }
    return left;
}

auto Parser::parse_logical_or() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::PipePipe, ASTBinaryOperator::LogicalOr},
    });
    return parse_left_associative(&Parser::parse_logical_and, operators);
}

auto Parser::parse_logical_and() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::AmpersandAmpersand, ASTBinaryOperator::LogicalAnd},
    });
    return parse_left_associative(&Parser::parse_bitwise_or, operators);
}

auto Parser::parse_bitwise_or() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::Pipe, ASTBinaryOperator::BitwiseOr},
    });
    return parse_left_associative(&Parser::parse_bitwise_xor, operators);
}

auto Parser::parse_bitwise_xor() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::Caret, ASTBinaryOperator::BitwiseXor},
    });
    return parse_left_associative(&Parser::parse_bitwise_and, operators);
}

auto Parser::parse_bitwise_and() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::Ampersand, ASTBinaryOperator::BitwiseAnd},
    });
    return parse_left_associative(&Parser::parse_comparison, operators);
}

auto Parser::comparison_operator(TokenKind kind) noexcept -> std::optional<ASTBinaryOperator> {
    switch (kind) {
        case TokenKind::EqualEqual:   return ASTBinaryOperator::Equal;
        case TokenKind::BangEqual:    return ASTBinaryOperator::NotEqual;
        case TokenKind::Less:         return ASTBinaryOperator::Less;
        case TokenKind::LessEqual:    return ASTBinaryOperator::LessEqual;
        case TokenKind::Greater:      return ASTBinaryOperator::Greater;
        case TokenKind::GreaterEqual: return ASTBinaryOperator::GreaterEqual;
        default:                      return std::nullopt;
    }
}

auto Parser::parse_comparison() noexcept -> std::optional<ASTExprID> {
    auto left = parse_shift();
    if (!left) {
        return std::nullopt;
    }
    if (failed) {
        return left;
    }
    const auto operation = comparison_operator(current().kind);
    if (!operation) {
        return left;
    }
    const auto token = consume();
    const auto right = parse_shift();
    if (!right) {
        return std::nullopt;
    }
    left = builder.append_expression(
        ASTExpr {
            .span = join(builder.expression(*left).span, builder.expression(*right).span),
            .value = ASTBinaryExpr {
                .left = *left,
                .op = *operation,
                .operator_span = token.span,
                .right = *right,
            },
        }
    );
    if (comparison_operator(current().kind).has_value()) {
        fail_here("comparison operators are non-associative");
    }
    return left;
}

auto Parser::parse_shift() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::LeftShift, ASTBinaryOperator::LeftShift},
        {TokenKind::RightShift, ASTBinaryOperator::RightShift},
    });
    return parse_left_associative(&Parser::parse_additive, operators);
}

auto Parser::parse_additive() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::Plus, ASTBinaryOperator::Add},
        {TokenKind::Minus, ASTBinaryOperator::Subtract},
    });
    return parse_left_associative(&Parser::parse_multiplicative, operators);
}

auto Parser::parse_multiplicative() noexcept -> std::optional<ASTExprID> {
    static constexpr auto operators = std::to_array<std::pair<TokenKind, ASTBinaryOperator>>({
        {TokenKind::Star, ASTBinaryOperator::Multiply},
        {TokenKind::Slash, ASTBinaryOperator::Divide},
        {TokenKind::Percent, ASTBinaryOperator::Remainder},
    });
    return parse_left_associative(&Parser::parse_cast_expression, operators);
}

auto Parser::parse_cast_expression() noexcept -> std::optional<ASTExprID> {
    auto operand = parse_prefix_expression();
    if (!operand) {
        return std::nullopt;
    }
    while (!failed && check(TokenKind::As)) {
        const auto operation = consume();
        const auto target_type = parse_type();
        if (!target_type) {
            return std::nullopt;
        }
        operand = builder.append_expression(
            ASTExpr {
                .span = join(builder.expression(*operand).span, builder.type(*target_type).span),
                .value = ASTCastExpr {
                    .operand_id = *operand,
                    .operator_span = operation.span,
                    .target_type = *target_type,
                },
            }
        );
    }
    return operand;
}

auto Parser::parse_prefix_expression() noexcept -> std::optional<ASTExprID> {
    if (check(TokenKind::Bang)
        || check(TokenKind::Minus)
        || check(TokenKind::Tilde)
        || check(TokenKind::Star)) {
        const auto nesting = enter_syntax_nesting();
        if (!nesting) {
            return std::nullopt;
        }
        const auto operation = consume();
        const auto operand = parse_prefix_expression();
        if (!operand) {
            return std::nullopt;
        }
        const auto op = operation.kind == TokenKind::Star ? ASTPrefixOperator::Dereference
            : operation.kind == TokenKind::Minus          ? ASTPrefixOperator::Negate
            : operation.kind == TokenKind::Tilde          ? ASTPrefixOperator::BitwiseNot
                                                          : ASTPrefixOperator::LogicalNot;
        return builder.append_expression(
            ASTExpr {
                .span = join(operation.span, builder.expression(*operand).span),
                .value = ASTPrefixExpr {
                    .op = op,
                    .operator_span = operation.span,
                    .operand_id = *operand,
                },
            }
        );
    }
    return parse_postfix_expression();
}

auto Parser::parse_postfix_expression() noexcept -> std::optional<ASTExprID> {
    auto operand = parse_primary_expression();
    if (!operand) {
        return std::nullopt;
    }
    while (!failed) {
        if (check(TokenKind::LeftParen)) {
            operand = parse_call(*operand);
            if (!operand) {
                return std::nullopt;
            }
            continue;
        }
        if (const auto left = match(TokenKind::LeftBracket)) {
            auto index = std::optional<ASTExprID> {};
            {
                const auto depth = enter_depth(expression_nesting);
                index = parse_expression();
            }
            const auto right = expect(TokenKind::RightBracket, "expected ']' after index");
            if (!index || failed) {
                return std::nullopt;
            }
            operand = builder.append_expression(
                ASTExpr {
                    .span = join(builder.expression(*operand).span, right.span),
                    .value = ASTIndexExpr {.operand_id = *operand, .index = *index},
                }
            );
            continue;
        }
        if (check(TokenKind::Dot) || check(TokenKind::ColonColon) || check(TokenKind::Arrow)) {
            const auto operation = consume();
            if (operation.kind == TokenKind::Arrow) {
                operand = builder.append_expression(
                    ASTExpr {
                        .span = join(builder.expression(*operand).span, operation.span),
                        .value = ASTPrefixExpr {
                            .op = ASTPrefixOperator::Dereference,
                            .operator_span = operation.span,
                            .operand_id = *operand
                        },
                    }
                );
            }
            const auto name = expect(TokenKind::Identifier, "expected member name");
            if (failed) {
                return std::nullopt;
            }
            operand = builder.append_expression(
                ASTExpr {
                    .span = join(builder.expression(*operand).span, name.span),
                    .value = ASTMemberExpr {
                        .operand_id = *operand,
                        .op = operation.kind != TokenKind::ColonColon ? ASTMemberOperator::Dot
                                                                      : ASTMemberOperator::Scope,
                        .operator_span = operation.span,
                        .name_span = name.span,
                    },
                }
            );
            continue;
        }
        if (const auto operation = match(TokenKind::Question)) {
            operand = builder.append_expression(
                ASTExpr {
                    .span = join(builder.expression(*operand).span, operation->span),
                    .value = ASTPropagationExpr {
                        .operand_id = *operand,
                        .operator_span = operation->span,
                    },
                }
            );
            continue;
        }
        break;
    }
    return operand;
}

auto Parser::parse_call(ASTExprID callee) noexcept -> std::optional<ASTExprID> {
    expect(TokenKind::LeftParen, "expected '('");
    auto arguments = std::vector<ASTCallArgument> {};
    {
        const auto depth = enter_depth(expression_nesting);
        while (!failed && !check(TokenKind::RightParen)) {
            const auto expression = parse_expression();
            if (!expression) {
                break;
            }
            arguments.push_back({
                .expression = *expression,
            });
            if (!match(TokenKind::Comma)) {
                break;
            }
            if (check(TokenKind::RightParen)) {
                break;
            }
        }
    }
    const auto right = expect(TokenKind::RightParen, "expected ')' after arguments");
    if (failed) {
        return std::nullopt;
    }
    return builder.append_expression(
        ASTExpr {
            .span = join(builder.expression(callee).span, right.span),
            .value = ASTCallExpr {
                .callee = callee,
                .arguments = std::move(arguments),
            },
        }
    );
}

auto Parser::parse_interpolation_parts(TokenKind closing) noexcept
    -> std::vector<ASTInterpolationPart> {
    auto parts = std::vector<ASTInterpolationPart>();
    while (!failed && !at_end() && !check(closing)) {
        if (check(TokenKind::InterpolationText)) {
            const auto* text =
                std::get_if<InterpolationTextValue>(&token_buffer->literal_value(cursor));
            auto bytes = text->bytes;
            const auto token = consume();
            parts.push_back(
                {.span = token.span, .value = ASTInterpolationText {.bytes = std::move(bytes)}}
            );
            continue;
        }
        const auto open = expect(TokenKind::InterpolationOpen, "expected interpolation hole");
        if (failed) {
            break;
        }
        const auto depth = enter_syntax_nesting();
        if (!depth) {
            break;
        }
        const auto expression_depth = enter_depth(expression_nesting);
        const auto expression = parse_expression();
        if (!expression) {
            break;
        }
        auto colon = std::optional<Span>();
        auto specification = std::vector<ASTInterpolationPart>();
        if (const auto token = match(TokenKind::InterpolationSpec)) {
            colon = token->span;
            specification = parse_interpolation_parts(TokenKind::InterpolationClose);
        }
        const auto close =
            expect(TokenKind::InterpolationClose, "expected '}' after interpolation expression");
        parts.push_back(
            {.span = join(open.span, close.span),
             .value = ASTInterpolationHole {
                 .expression = *expression,
                 .colon_span = colon,
                 .specification = std::move(specification)
             }}
        );
    }
    return parts;
}

auto Parser::parse_primary_expression() noexcept -> std::optional<ASTExprID> {
    if (const auto open = match(TokenKind::InterpolationStart)) {
        auto parts = parse_interpolation_parts(TokenKind::InterpolationEnd);
        const auto close =
            expect(TokenKind::InterpolationEnd, "expected closing interpolation quote");
        if (failed) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = join(open->span, close.span),
                .value = ASTInterpolationExpr {.parts = std::move(parts)},
            }
        );
    }
    if (check(TokenKind::NumberLiteral)
        || check(TokenKind::StringLiteral)
        || check(TokenKind::CStringLiteral)
        || check(TokenKind::CharLiteral)
        || check(TokenKind::True)
        || check(TokenKind::False)
        || check(TokenKind::Nullptr)) {
        auto value = consume_literal();
        return builder.append_expression(
            ASTExpr {
                .span = value.span,
                .value = std::move(value),
            }
        );
    }
    if (check(TokenKind::ColonColon)) {
        if (construction_allowed_here()) {
            if (const auto construction = try_parse_construction()) {
                return *construction;
            }
        }
        const auto root = consume();
        auto components = std::vector<Span>();
        auto end = root.span;
        do {
            end = expect(TokenKind::Identifier, "expected C++ name after '::'").span;
            components.push_back(end);
        } while (!failed && match(TokenKind::ColonColon));
        if (failed) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = join(root.span, end),
                .value =
                    ASTCppNameExpr {.global_root = root.span, .components = std::move(components)}
            }
        );
    }
    if (check(TokenKind::Identifier)) {
        if (construction_allowed_here()) {
            if (const auto construction = try_parse_construction()) {
                return *construction;
            }
        }
        const auto name = consume();
        return builder.append_expression(
            ASTExpr {
                .span = name.span,
                .value = ASTNameExpr {.name_span = name.span},
            }
        );
    }
    if (const auto dot = match(TokenKind::Dot)) {
        const auto name = expect(TokenKind::Identifier, "expected case name after '.'");
        if (failed) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = join(dot->span, name.span),
                .value = ASTContextualCaseExpr {
                    .dot_span = dot->span,
                    .name_span = name.span,
                },
            }
        );
    }
    if (check(TokenKind::Fn)) {
        if (construction_allowed_here()) {
            if (const auto construction = try_parse_construction()) {
                return *construction;
            }
        }
        fail_here("function type in expression position must construct a value");
        return std::nullopt;
    }
    if (const auto left = match(TokenKind::LeftParen)) {
        auto expression = std::optional<ASTExprID> {};
        {
            const auto depth = enter_depth(expression_nesting);
            expression = parse_expression();
        }
        const auto right = expect(TokenKind::RightParen, "expected ')' after grouped expression");
        if (!expression || failed) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = join(left->span, right.span),
                .value = ASTGroupExpr {.expression = *expression},
            }
        );
    }
    if (check(TokenKind::LeftBracket) && lambda_starts_here()) {
        return parse_lambda_expression();
    }
    if (const auto left = match(TokenKind::LeftBracket)) {
        auto elements = std::vector<ASTExprID> {};
        {
            const auto depth = enter_depth(expression_nesting);
            while (!failed && !check(TokenKind::RightBracket)) {
                auto element = parse_expression();
                if (!element) {
                    break;
                }
                elements.push_back(*element);
                if (!match(TokenKind::Comma)) {
                    break;
                }
                if (check(TokenKind::RightBracket)) {
                    break;
                }
            }
        }
        const auto right = expect(TokenKind::RightBracket, "expected ']' after array expression");
        if (failed) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = join(left->span, right.span),
                .value = ASTArrayExpr {.element_ids = std::move(elements)},
            }
        );
    }
    if (check(TokenKind::If)) {
        auto form = parse_if_form();
        if (!form) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = form->span,
                .value = std::move(*form),
            }
        );
    }
    if (check(TokenKind::Match)) {
        auto form = parse_match_form();
        if (!form) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = form->span,
                .value = std::move(*form),
            }
        );
    }
    if (check(TokenKind::Try)) {
        auto form = parse_try_form();
        if (!form) {
            return std::nullopt;
        }
        return builder.append_expression(
            ASTExpr {
                .span = form->span,
                .value = std::move(*form),
            }
        );
    }
    fail_here("expected expression");
    return std::nullopt;
}

auto Parser::lambda_starts_here() const noexcept -> bool {
    if (!check(TokenKind::LeftBracket)) {
        return false;
    }
    auto index = cursor + 1;
    if (index >= tokens.size()) {
        return false;
    }
    if (tokens[index].kind == TokenKind::RightBracket) {
        return index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LeftParen;
    }
    while (index < tokens.size()) {
        if (tokens[index].kind == TokenKind::Ampersand) {
            ++index;
        }
        if (index >= tokens.size() || tokens[index].kind != TokenKind::Identifier) {
            return false;
        }
        ++index;
        if (index >= tokens.size()) {
            return false;
        }
        if (tokens[index].kind == TokenKind::RightBracket) {
            return index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LeftParen;
        }
        if (tokens[index].kind != TokenKind::Comma) {
            return false;
        }
        ++index;
        if (index < tokens.size() && tokens[index].kind == TokenKind::RightBracket) {
            return index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LeftParen;
        }
    }
    return false;
}

auto Parser::parse_callable_body() noexcept -> std::optional<ASTCallableBody> {
    if (const auto arrow = match(TokenKind::FatArrow)) {
        const auto expression = parse_expression();
        if (!expression || failed) {
            return std::nullopt;
        }
        return ASTExpressionBody {.arrow_span = arrow->span, .expression = *expression};
    }
    const auto block = parse_ordinary_block();
    if (!block || failed) {
        return std::nullopt;
    }
    return *block;
}

auto Parser::callable_body_span(const ASTCallableBody& body) const noexcept -> Span {
    if (const auto* block = std::get_if<ASTBlockID>(&body)) {
        return builder.block(*block).span;
    }
    const auto& expression = std::get<ASTExpressionBody>(body);
    return join(expression.arrow_span, builder.expression(expression.expression).span);
}

auto Parser::parse_lambda_expression() noexcept -> std::optional<ASTExprID> {
    const auto left = expect(TokenKind::LeftBracket, "expected '[' in lambda capture list");
    auto captures = std::vector<ASTLambdaCapture> {};
    while (!failed && !check(TokenKind::RightBracket)) {
        const auto access = match(TokenKind::Ampersand);
        const auto name = expect(TokenKind::Identifier, "expected capture name");
        captures.push_back({
            .span = access ? join(access->span, name.span) : name.span,
            .write_marker = access.transform([](const Token& token) static { return token.span; }),
            .name_span = name.span,
        });
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightBracket)) {
            break;
        }
    }
    expect(TokenKind::RightBracket, "expected ']' after lambda captures");
    expect(TokenKind::LeftParen, "expected '(' after lambda captures");

    auto parameters = std::vector<ASTFunctionParameter> {};
    while (!failed && !check(TokenKind::RightParen)) {
        auto access = ASTAccessSyntax {.mode = ASTAccessMode::Read, .marker = std::nullopt};
        if (const auto marker = match(TokenKind::Ampersand)) {
            access = {.mode = ASTAccessMode::Write, .marker = marker->span};
        } else if (const auto marker = match(TokenKind::AmpersandAmpersand)) {
            access = {.mode = ASTAccessMode::Take, .marker = marker->span};
        }
        const auto name = expect(TokenKind::Identifier, "expected lambda parameter name");
        auto type = std::optional<ASTTypeID> {};
        if (match(TokenKind::Colon)) {
            type = parse_type();
        }
        const auto start = access.marker.value_or(name.span);
        parameters.push_back({
            .span =
                type.has_value() ? join(start, builder.type(*type).span) : join(start, name.span),
            .access = access,
            .target = slice(source, name.span) == "_" ? ASTBindingTarget {ASTDiscardBindingTarget {
                                                            .underscore_span = name.span,
                                                        }}
                                                      : ASTBindingTarget {ASTNamedBindingTarget {
                                                            .name_span = name.span,
                                                        }},
            .type = type,
        });
        if (!match(TokenKind::Comma)) {
            break;
        }
        if (check(TokenKind::RightParen)) {
            break;
        }
    }
    expect(TokenKind::RightParen, "expected ')' after lambda parameters");

    auto result_type = std::optional<ASTTypeID> {};
    if (match(TokenKind::Arrow)) {
        result_type = parse_type();
    }
    auto throw_clause = std::optional<ASTThrowClause> {};
    if (check(TokenKind::Throw)) {
        throw_clause = parse_throw_clause();
    }
    const auto test_context = enter_test_statement_context(false);
    const auto body = parse_callable_body();
    if (!body || failed) {
        return std::nullopt;
    }
    return builder.append_expression(
        ASTExpr {
            .span = join(left.span, callable_body_span(*body)),
            .value = ASTLambdaExpr {
                .captures = std::move(captures),
                .parameters = std::move(parameters),
                .result_type = result_type,
                .throw_clause = std::move(throw_clause),
                .body = *body,
            },
        }
    );
}

auto Parser::construction_allowed_here() const noexcept -> bool {
    return block_boundary_depth == 0 || expression_nesting != 0;
}

auto Parser::try_parse_construction() noexcept -> std::optional<ASTExprID> {
    const auto checkpoint = begin_speculation();
    const auto start = current().span;
    auto construction_type = std::optional<ASTConstructionType> {};
    if (check(TokenKind::Identifier) || check(TokenKind::ColonColon)) {
        auto parsed = parse_named_type_form();
        construction_type = ASTConstructionType {
            .span = parsed.span,
            .value = std::move(parsed.value),
        };
    } else if (check(TokenKind::Fn)) {
        auto parsed = parse_function_type_form();
        if (parsed.has_value()) {
            construction_type = ASTConstructionType {
                .span = parsed->span,
                .value = std::move(parsed->value),
            };
        }
    } else {
        fail_here("expected construction type");
    }

    if (!failed) {
        expect(TokenKind::LeftBrace, "expected '{' after construction type");
    }
    auto initializer = ASTConstructionInitializer {.value = std::monostate {}};
    auto right = current();
    if (!failed && !check(TokenKind::RightBrace)) {
        const auto depth = enter_depth(expression_nesting);
        if (check(TokenKind::Identifier)) {
            const auto lookahead = cursor + 1;
            const auto field_form =
                lookahead < tokens.size() && tokens[lookahead].kind == TokenKind::Colon;
            if (field_form) {
                auto fields = std::vector<ASTFieldInitializer> {};
                const auto list_start = current().span;
                while (!failed) {
                    const auto name =
                        expect(TokenKind::Identifier, "expected initializer field name");
                    expect(TokenKind::Colon, "expected ':' after initializer field name");
                    const auto value = parse_expression();
                    if (!value) {
                        break;
                    }
                    fields.push_back({
                        .span = join(name.span, builder.expression(*value).span),
                        .name_span = name.span,
                        .value = *value,
                    });
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                    if (check(TokenKind::RightBrace)) {
                        break;
                    }
                }
                const auto list_end = fields.empty() ? list_start : fields.back().span;
                initializer = {
                    .value = ASTFieldInitializerList {
                        .span = join(list_start, list_end),
                        .fields = std::move(fields),
                    },
                };
            } else {
                auto values = std::vector<ASTExprID> {};
                const auto list_start = current().span;
                while (!failed) {
                    const auto value = parse_expression();
                    if (!value) {
                        break;
                    }
                    values.push_back(*value);
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                    if (check(TokenKind::RightBrace)) {
                        break;
                    }
                }
                const auto list_end =
                    values.empty() ? list_start : builder.expression(values.back()).span;
                initializer = {
                    .value = ASTPositionalInitializerList {
                        .span = join(list_start, list_end),
                        .values = std::move(values),
                    },
                };
            }
        } else {
            auto values = std::vector<ASTExprID> {};
            const auto list_start = current().span;
            while (!failed) {
                const auto value = parse_expression();
                if (!value) {
                    break;
                }
                values.push_back(*value);
                if (!match(TokenKind::Comma)) {
                    break;
                }
                if (check(TokenKind::RightBrace)) {
                    break;
                }
            }
            const auto list_end =
                values.empty() ? list_start : builder.expression(values.back()).span;
            initializer = {
                .value = ASTPositionalInitializerList {
                    .span = join(list_start, list_end),
                    .values = std::move(values),
                },
            };
        }
    }
    if (!failed) {
        right = expect(TokenKind::RightBrace, "expected '}' after construction initializer");
    }

    const auto success = !failed;
    finish_speculation(checkpoint, success);
    if (!success) {
        return std::nullopt;
    }
    return builder.append_expression(
        ASTExpr {
            .span = join(start, right.span),
            .value = ASTConstructionExpr {
                .type = *construction_type,
                .initializer = initializer,
            },
        }
    );
}
