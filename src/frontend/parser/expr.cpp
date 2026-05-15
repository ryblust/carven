module zero.frontend.parser:expr;

import :impl;
import zero.common.source;
import zero.frontend.token;
import zero.frontend.parser.ast;
import std;

auto Parser::parse_assignment_expr() noexcept -> Expr* {
    const auto lhs = parse_binary_expr(PrecedenceLevel::LogicalOr);

    if (check(TokenKind::Equal)) {
        const auto eq_token = advance();
        const auto rhs = parse_assignment_expr();
        return make_expr(AssignExpr{ .lhs = lhs, .eq = eq_token->span, .rhs = rhs });
    }

    const auto* next = peek();
    if (next != nullptr) {
        if (const auto op = compound_assign_op(next->kind)) {
            const auto op_token = advance();
            const auto rhs = parse_assignment_expr();
            return make_expr(CompoundAssignExpr{ .lhs = lhs, .op = *op, .span = op_token->span, .rhs = rhs });
        }
    }

    return lhs;
}

auto Parser::parse_binary_expr(PrecedenceLevel level) noexcept -> Expr* {
    auto lhs = parse_next_tighter(level);
    while (true) {
        const auto* token = peek();
        if (token == nullptr) break;
        const auto op = token_to_binop(token->kind, level);
        if (!op) break;
        const auto span = token->span;
        advance();
        const auto rhs = parse_next_tighter(level);
        lhs = make_expr(BinaryExpr{ .lhs = lhs, .op = *op, .span = span, .rhs = rhs });
    }
    return lhs;
}

auto Parser::parse_next_tighter(PrecedenceLevel level) noexcept -> Expr* {
    switch (level) {
        case PrecedenceLevel::LogicalOr:      return parse_binary_expr(PrecedenceLevel::LogicalAnd);
        case PrecedenceLevel::LogicalAnd:     return parse_binary_expr(PrecedenceLevel::BitwiseOr);
        case PrecedenceLevel::BitwiseOr:      return parse_binary_expr(PrecedenceLevel::BitwiseXor);
        case PrecedenceLevel::BitwiseXor:     return parse_binary_expr(PrecedenceLevel::BitwiseAnd);
        case PrecedenceLevel::BitwiseAnd:     return parse_binary_expr(PrecedenceLevel::Equality);
        case PrecedenceLevel::Equality:       return parse_binary_expr(PrecedenceLevel::Relational);
        case PrecedenceLevel::Relational:     return parse_binary_expr(PrecedenceLevel::Shift);
        case PrecedenceLevel::Shift:          return parse_binary_expr(PrecedenceLevel::Additive);
        case PrecedenceLevel::Additive:       return parse_binary_expr(PrecedenceLevel::Multiplicative);
        case PrecedenceLevel::Multiplicative: return parse_prefix_expr();
    }
    return parse_prefix_expr();
}

auto Parser::parse_prefix_expr() noexcept -> Expr* {
    const auto* token = peek();
    if (token == nullptr) {
        return make_expr(LiteralExpr{ eof_span() });
    }

    switch (token->kind) {
        using enum TokenKind;
        case Bang: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::LogicalNot, .span = op_span, .rhs = parse_prefix_expr() });
        }
        case Minus: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::Neg, .span = op_span, .rhs = parse_prefix_expr() });
        }
        case Tilde: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::BitNot, .span = op_span, .rhs = parse_prefix_expr() });
        }
        case PlusPlus: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::PreInc, .span = op_span, .rhs = parse_prefix_expr() });
        }
        case MinusMinus: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::PreDec, .span = op_span, .rhs = parse_prefix_expr() });
        }
        case Ampersand: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::AddressOf, .span = op_span, .rhs = parse_prefix_expr() });
        }
        case Star: {
            const auto op_span = advance()->span;
            return make_expr(PrefixExpr{ .op = UnaryOp::Deref, .span = op_span, .rhs = parse_prefix_expr() });
        }
        default: return parse_postfix_expr();
    }
}

auto Parser::parse_postfix_expr() noexcept -> Expr* {
    auto expr = parse_primary_expr();
    return parse_postfix_ops(expr);
}

auto Parser::parse_postfix_ops(Expr* lhs) noexcept -> Expr* {
    while (true) {
        const auto* token = peek();
        if (token == nullptr) break;

        switch (token->kind) {
            using enum TokenKind;
            case LeftParen: {
                const auto lparen = advance()->span;
                auto args = std::vector<Expr*>();
                if (!check(RightParen)) {
                    args.emplace_back(parse_assignment_expr());
                    while (check(Comma)) {
                        advance();
                        args.emplace_back(parse_assignment_expr());
                    }
                }
                const auto rparen = expect(RightParen, "expected ')' after function arguments");
                if (!rparen) {
                    lhs = make_expr(LiteralExpr{ lparen });
                    break;
                }
                lhs = make_expr(CallExpr{ .callee = lhs, .lparen = lparen, .args = std::move(args), .rparen = rparen->span });
                break;
            }
            case LeftBracket: {
                const auto lbracket = advance()->span;
                const auto index = parse_expr();
                const auto rbracket = expect(RightBracket, "expected ']' after index");
                if (!rbracket) {
                    lhs = make_expr(LiteralExpr{ lbracket });
                    break;
                }
                lhs = make_expr(IndexExpr{ .lhs = lhs, .lbracket = lbracket, .index = index, .rbracket = rbracket->span });
                break;
            }
            case Dot:
            case Arrow:
            case ColonColon: {
                const auto op_span = advance()->span;
                const auto field_name = expect_name("expected field name after '" + std::string(text_at(source, op_span)) + "'");
                if (!field_name) {
                    lhs = make_expr(LiteralExpr{ op_span });
                    break;
                }
                lhs = make_expr(FieldExpr{ .lhs = lhs, .dot = op_span, .field = field_name->span });
                break;
            }
            case PlusPlus: {
                const auto op_span = advance()->span;
                lhs = make_expr(PostfixExpr{ .lhs = lhs, .op = PostfixOp::PostInc, .span = op_span });
                break;
            }
            case MinusMinus: {
                const auto op_span = advance()->span;
                lhs = make_expr(PostfixExpr{ .lhs = lhs, .op = PostfixOp::PostDec, .span = op_span });
                break;
            }
            default: return lhs;
        }
    }
    return lhs;
}

auto Parser::parse_primary_expr() noexcept -> Expr* {
    const auto* token = peek();
    if (token == nullptr) {
        return make_expr(LiteralExpr{ eof_span() });
    }

    switch (token->kind) {
        using enum TokenKind;
        case NumberLiteral:
        case StringLiteral:
        case CharLiteral: {
            const auto span = advance()->span;
            return make_expr(LiteralExpr{ span });
        }
        case Keyword: {
            const auto keyword = text_at(source, token->span);
            if (keyword == "true" || keyword == "false") {
                const auto span = advance()->span;
                return make_expr(LiteralExpr{ span });
            }
            if (keyword == "if") return parse_if_expr();
            push_error("keyword '" + std::string(keyword) + "' cannot be used as an identifier", token->span);
            advance();
            return make_expr(LiteralExpr{ token->span });
        }
        case Identifier: {
            const auto span = advance()->span;
            return make_expr(IdentExpr{ span });
        }
        case LeftParen: {
            const auto lparen = advance()->span;
            const auto inner = parse_expr();
            const auto rparen = expect(RightParen, "expected ')' after grouped expression");
            if (!rparen) {
                return make_expr(LiteralExpr{ lparen });
            }
            return make_expr(GroupExpr{ .lparen = lparen, .inner = inner, .rparen = rparen->span });
        }
        default: {
            const auto span = token->span;
            push_error("unexpected token in expression", span);
            advance();
            return make_expr(LiteralExpr{ span });
        }
    }
}

auto Parser::parse_if_expr() noexcept -> Expr* {
    const auto keyword = advance();

    const auto condition = parse_expr();

    if (!check(TokenKind::LeftBrace)) {
        push_error("expected '{' after if condition", current_span());
        return make_expr(LiteralExpr{ keyword->span });
    }

    auto then_branch = parse_block();
    if (!then_branch) {
        return make_expr(LiteralExpr{ keyword->span });
    }

    std::optional<Span> else_kw;
    std::optional<BlockStmt> else_branch;

    if (const auto* next = peek(); next != nullptr && is_keyword(*next, "else")) {
        else_kw = advance()->span;

        if (!check(TokenKind::LeftBrace)) {
            push_error("expected '{' after else", current_span());
            return make_expr(LiteralExpr{ keyword->span });
        }
        auto parsed_else = parse_block();
        if (!parsed_else) {
            return make_expr(LiteralExpr{ keyword->span });
        }
        else_branch = std::move(*parsed_else);
    }

    return make_expr(IfExpr {
        .keyword = keyword->span,
        .condition = condition,
        .then_branch = std::move(*then_branch),
        .else_kw = else_kw,
        .else_branch = std::move(else_branch)
    });
}
