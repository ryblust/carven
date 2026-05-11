module zero.frontend.parser:decl;

import :impl;
import zero.common.source;
import zero.frontend.lexer.token;
import zero.frontend.parser.ast;
import std;

auto Parser::parse_import() noexcept -> std::optional<ImportItem> {
    advance();

    const auto module_name = expect_name("expected import module name");
    if (!module_name) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    auto item = ImportItem {
        .module_name = module_name->span,
        .using_decls = {}
    };

    if (match_keyword("using")) {
        if (match(TokenKind::LeftBrace)) {
            while (!eof() && !check(TokenKind::RightBrace)) {
                const auto decl = expect_name("expected using declaration name");
                if (decl) item.using_decls.emplace_back(decl->span);

                if (!match(TokenKind::Comma) && !check(TokenKind::RightBrace)) {
                    push_error("expected ',' or '}' in using declaration list", current_span());
                    synchronize_to_item_end();
                    return std::nullopt;
                }
            }

            if (!expect(TokenKind::RightBrace, "expected '}' after using declaration list")) {
                synchronize_to_item_end();
                return std::nullopt;
            }
        } else {
            const auto decl = expect_name("expected using declaration name");
            if (!decl) {
                synchronize_to_item_end();
                return std::nullopt;
            }
            item.using_decls.emplace_back(decl->span);
        }
    }

    if (!expect(TokenKind::SemiColon, "expected ';' after import")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    return item;
}

auto Parser::parse_enum() noexcept -> std::optional<EnumItem> {
    advance();

    const auto name = expect_name("expected enum name");
    if (!name) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    auto item = EnumItem {
        .name = name->span,
        .size = {},
        .fields = {}
    };

    if (match(TokenKind::Colon)) {
        const auto size = expect_type("expected enum size type");
        if (!size) {
            synchronize_to_item_end();
            return std::nullopt;
        }
        item.size = size->span;
    }

    if (!expect(TokenKind::LeftBrace, "expected '{' before enum fields")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    while (!eof() && !check(TokenKind::RightBrace)) {
        const auto field = expect_name("expected enum field name");
        if (field) item.fields.emplace_back(field->span);

        if (match(TokenKind::Comma)) continue;
        if (!check(TokenKind::RightBrace)) {
            push_error("expected ',' or '}' after enum field", current_span());
            synchronize_to_item_end();
            return std::nullopt;
        }
    }

    if (!expect(TokenKind::RightBrace, "expected '}' after enum fields")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    return item;
}

auto Parser::parse_struct() noexcept -> std::optional<StructItem> {
    advance();

    const auto name = expect_name("expected struct name");
    if (!name) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    auto item = StructItem {
        .name = name->span,
        .fields = {}
    };

    if (!expect(TokenKind::LeftBrace, "expected '{' before struct fields")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    while (!eof() && !check(TokenKind::RightBrace)) {
        const auto field_name = expect_name("expected struct field name");
        if (!field_name) {
            synchronize_to_item_end();
            return std::nullopt;
        }

        if (!expect(TokenKind::Colon, "expected ':' after struct field name")) {
            synchronize_to_item_end();
            return std::nullopt;
        }

        const auto field_type = expect_type("expected struct field type");
        if (!field_type) {
            synchronize_to_item_end();
            return std::nullopt;
        }

        item.fields.emplace_back(field_name->span, field_type->span);

        if (match(TokenKind::Comma) || match(TokenKind::SemiColon)) continue;
        if (!check(TokenKind::RightBrace)) {
            push_error("expected ',' or '}' after struct field", current_span());
            synchronize_to_item_end();
            return std::nullopt;
        }
    }

    if (!expect(TokenKind::RightBrace, "expected '}' after struct fields")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    return item;
}

auto Parser::parse_params() noexcept -> std::optional<std::vector<FunctionParam>> {
    auto params = std::vector<FunctionParam>();

    while (!eof() && !check(TokenKind::RightParen)) {
        const auto param_name = expect_name("expected function parameter name");
        if (!param_name) return std::nullopt;

        if (!expect(TokenKind::Colon, "expected ':' after function parameter name")) {
            return std::nullopt;
        }

        const auto param_type = expect_type("expected function parameter type");
        if (!param_type) return std::nullopt;

        params.emplace_back(param_name->span, param_type->span);

        if (match(TokenKind::Comma)) continue;
        if (!check(TokenKind::RightParen)) {
            push_error("expected ',' or ')' after function parameter", current_span());
            return std::nullopt;
        }
    }

    return params;
}

auto Parser::parse_function() noexcept -> std::optional<FunctionItem> {
    advance();

    const auto name = expect_name("expected function name");
    if (!name) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    if (!expect(TokenKind::LeftParen, "expected '(' after function name")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    auto params = parse_params();
    if (!params) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    if (!expect(TokenKind::RightParen, "expected ')' after function parameters")) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    auto return_type = Span();
    if (match(TokenKind::Arrow)) {
        const auto type = expect_type("expected function return type");
        if (!type) {
            synchronize_to_item_end();
            return std::nullopt;
        }
        return_type = type->span;
    }

    auto block = parse_block();
    if (!block) {
        synchronize_to_item_end();
        return std::nullopt;
    }

    auto* block_ptr = make_stmt(std::move(*block));
    auto& block_stmt = std::get<BlockStmt>(*block_ptr);
    return FunctionItem {
        .name = name->span,
        .params = std::move(*params),
        .return_type = return_type,
        .body = &block_stmt
    };
}
