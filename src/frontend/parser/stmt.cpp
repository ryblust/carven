module zero.frontend.parser:stmt;

import :impl;
import zero.common.source;
import zero.frontend.token;
import zero.frontend.parser.ast;
import std;

auto Parser::parse_stmt() noexcept -> Stmt* {
    const auto* token = peek();
    if (token == nullptr) return nullptr;

    if (token->kind == TokenKind::SemiColon) {
        const auto span = advance()->span;
        return make_stmt(EmptyStmt{ span });
    }

    if (token->kind == TokenKind::Keyword) {
        const auto keyword = text_at(source, token->span);
        if (keyword == "let" || keyword == "var" || keyword == "const") return parse_var_decl();
        if (keyword == "return") return parse_return_stmt();
        if (keyword == "while") return parse_while_stmt();
        if (keyword == "for") return parse_for_stmt();
        if (keyword == "if") {
            const auto expr = parse_if_expr();
            return make_stmt(ExprStmt{ .expr = expr, .semicolon = Span{} });
        }
    }

    return parse_expr_stmt();
}

auto Parser::parse_expr_stmt() noexcept -> Stmt* {
    const auto expr = parse_expr();
    if (const auto semi = match(TokenKind::SemiColon)) {
        return make_stmt(ExprStmt{ .expr = expr, .semicolon = semi->span });
    }
    // Semicolon is optional at end of block or end of input
    if (check(TokenKind::RightBrace) || eof()) {
        return make_stmt(ExprStmt{ .expr = expr, .semicolon = Span{} });
    }
    push_error("expected ';' after expression", current_span());
    return make_stmt(EmptyStmt{ eof_span() });
}

auto Parser::parse_var_decl() noexcept -> Stmt* {
    const auto keyword = advance();
    const auto name = expect_name("expected variable name");
    if (!name) {
        synchronize_to_stmt_end();
        return nullptr;
    }

    auto type_span = Span();
    if (check(TokenKind::Colon)) {
        advance();
        const auto type = expect_type("expected type after ':'");
        if (!type) {
            synchronize_to_stmt_end();
            return nullptr;
        }
        type_span = type->span;
    }

    auto eq_span = Span();
    Expr* init = nullptr;
    if (check(TokenKind::Equal)) {
        eq_span = advance()->span;
        init = parse_expr();
    }

    const auto keyword_text = text_at(source, keyword->span);
    if ((keyword_text == "let" || keyword_text == "const") && init == nullptr) {
        push_error(std::string(keyword_text) + " requires an initializer", keyword->span);
    }

    const auto semi = expect(TokenKind::SemiColon, "expected ';' after variable declaration");
    if (!semi) {
        synchronize_to_stmt_end();
        return nullptr;
    }

    return make_stmt(VarDecl {
        .keyword = keyword->span,
        .name = name->span,
        .type = type_span,
        .eq = eq_span,
        .init = init,
        .semicolon = semi->span
    });
}

auto Parser::parse_return_stmt() noexcept -> Stmt* {
    const auto keyword = advance();
    Expr* value = nullptr;
    if (!check(TokenKind::SemiColon)) {
        value = parse_expr();
    }
    const auto semi = expect(TokenKind::SemiColon, "expected ';' after return");
    if (!semi) {
        synchronize_to_stmt_end();
        return nullptr;
    }
    return make_stmt(ReturnStmt{ .keyword = keyword->span, .value = value, .semicolon = semi->span });
}

auto Parser::parse_stmt_or_block() noexcept -> Stmt* {
    if (check(TokenKind::LeftBrace)) {
        if (auto block = parse_block()) return make_stmt(std::move(*block));
        return nullptr;
    }
    return parse_stmt();
}

auto Parser::parse_while_stmt() noexcept -> Stmt* {
    const auto keyword = advance();
    const auto condition = parse_expr();
    if (const auto body = parse_stmt_or_block()) {
        return make_stmt(WhileStmt {
            .keyword = keyword->span,
            .condition = condition,
            .body = body
        });
    }
    return nullptr;
}

auto Parser::parse_for_stmt() noexcept -> Stmt* {
    const auto keyword = advance();
    const auto lparen = expect(TokenKind::LeftParen, "expected '(' after for");
    if (!lparen) { synchronize_to_stmt_end(); return nullptr; }

    ForInit* for_init = nullptr;
    auto init_semi_span = Span();
    if (!check(TokenKind::SemiColon)) {
        const auto* token = peek();
        if (token != nullptr && token->kind == TokenKind::Keyword) {
            const auto keyword_text = text_at(source, token->span);
            if (keyword_text == "let" || keyword_text == "var" || keyword_text == "const") {
                if (auto decl = parse_var_decl()) {
                    auto& var_decl = std::get<VarDecl>(*decl);
                    init_semi_span = var_decl.semicolon;
                    for_init = make_for_init(std::move(var_decl));
                }
            } else {
                const auto expr = parse_expr();
                for_init = make_for_init(ExprStmt{ .expr = expr, .semicolon = Span{} });
            }
        } else {
            const auto expr = parse_expr();
            for_init = make_for_init(ExprStmt{ .expr = expr, .semicolon = Span{} });
        }
    }
    if (!is_empty(init_semi_span)) {
        // semicolon was consumed by parse_var_decl
    } else {
        const auto init_semi = expect(TokenKind::SemiColon, "expected ';' after for-init");
        if (!init_semi) { synchronize_to_stmt_end(); return nullptr; }
        init_semi_span = init_semi->span;
    }

    Expr* condition = nullptr;
    if (!check(TokenKind::SemiColon)) {
        condition = parse_expr();
    }
    const auto cond_semi = expect(TokenKind::SemiColon, "expected ';' after for-condition");
    if (!cond_semi) { synchronize_to_stmt_end(); return nullptr; }

    Expr* step = nullptr;
    if (!check(TokenKind::RightParen)) {
        step = parse_expr();
    }
    const auto rparen = expect(TokenKind::RightParen, "expected ')' after for-step");
    if (!rparen) { synchronize_to_stmt_end(); return nullptr; }

    if (const auto body = parse_stmt_or_block()) {
        return make_stmt(ForStmt {
            .keyword = keyword->span, .lparen = lparen->span,
            .init = for_init, .init_semi = init_semi_span,
            .condition = condition, .cond_semi = cond_semi->span,
            .step = step, .rparen = rparen->span, .body = body
        });
    }
    return nullptr;
}
