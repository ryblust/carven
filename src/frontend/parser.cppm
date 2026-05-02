export module zero.frontend.parser;

import zero.common.source;
import zero.frontend.token;
import std;

export struct ParseError final {
    std::string message;
    Span span;
};

export struct ImportItem final {
    Span module_name;
    std::vector<Span> using_decls;
};

export struct EnumItem final {
    Span name;
    Span size;
    std::vector<Span> fields;
};

export struct StructField final {
    Span name;
    Span type;
};

export struct StructItem final {
    Span name;
    std::vector<StructField> fields;
};

export struct FunctionParam final {
    Span name;
    Span type;
};

export struct FunctionItem final {
    Span name;
    std::vector<FunctionParam> params;
    Span return_type;
    Span body_span;
};

export using TopLevelItem = std::variant<ImportItem, EnumItem, StructItem, FunctionItem>;

export struct ParseResult final {
    std::vector<TopLevelItem> items;
    std::vector<ParseError> errors;

    constexpr auto has_errors() const noexcept -> bool {
        return !errors.empty();
    }
};

export auto format_parse_error(const ParseError& error) noexcept -> std::string {
    return std::format("error: {} [{}..{}]", error.message, error.span.start, error.span.end);
}

class Parser final {
public:
    constexpr Parser(std::span<const Token> t, std::string_view s) noexcept : tokens(t), source(s) {}

    auto parse() noexcept -> ParseResult {
        while (!eof()) {
            const auto* token = peek();
            if (token == nullptr) break;

            if (is_keyword(*token, "import")) {
                if (auto item = parse_import()) result.items.emplace_back(std::move(*item));
            } else if (is_keyword(*token, "enum")) {
                if (auto item = parse_enum()) result.items.emplace_back(std::move(*item));
            } else if (is_keyword(*token, "struct")) {
                if (auto item = parse_struct()) result.items.emplace_back(std::move(*item));
            } else if (is_keyword(*token, "fn")) {
                if (auto item = parse_function()) result.items.emplace_back(std::move(*item));
            } else {
                push_error("expected top-level item", token->span);
                advance();
                synchronize_top_level();
            }
        }

        return std::move(result);
    }

private:
    std::span<const Token> tokens;
    std::string_view source;
    std::size_t current = 0;
    ParseResult result;

    constexpr auto eof() const noexcept -> bool {
        return current >= tokens.size();
    }

    constexpr auto eof_span() const noexcept -> Span {
        const auto pos = static_cast<std::uint32_t>(source.size());
        return { .start = pos, .end = pos };
    }

    constexpr auto current_span() const noexcept -> Span {
        if (const auto* token = peek()) return token->span;
        return eof_span();
    }

    constexpr auto peek(std::size_t offset = 0) const noexcept -> const Token* {
        const auto index = current + offset;
        return index < tokens.size() ? &tokens[index] : nullptr;
    }

    auto advance() noexcept -> std::optional<Token> {
        if (eof()) return std::nullopt;
        return tokens[current++];
    }

    constexpr auto check(TokenKind kind) const noexcept -> bool {
        const auto* token = peek();
        return token != nullptr && token->kind == kind;
    }

    auto match(TokenKind kind) noexcept -> std::optional<Token> {
        if (!check(kind)) return std::nullopt;
        return advance();
    }

    constexpr auto is_keyword(Token token, std::string_view keyword) const noexcept -> bool {
        return token.kind == TokenKind::Keyword && text_at(source, token.span) == keyword;
    }

    constexpr auto check_keyword(std::string_view keyword) const noexcept -> bool {
        const auto* token = peek();
        return token != nullptr && is_keyword(*token, keyword);
    }

    auto match_keyword(std::string_view keyword) noexcept -> std::optional<Token> {
        if (!check_keyword(keyword)) return std::nullopt;
        return advance();
    }

    auto expect(TokenKind kind, std::string message) noexcept -> std::optional<Token> {
        if (auto token = match(kind)) return token;
        push_error(std::move(message), current_span());
        return std::nullopt;
    }

    auto expect_name(std::string message) noexcept -> std::optional<Token> {
        return expect(TokenKind::Identifier, std::move(message));
    }

    auto expect_type(std::string message) noexcept -> std::optional<Token> {
        const auto* token = peek();
        if (token != nullptr && (token->kind == TokenKind::Identifier || token->kind == TokenKind::Keyword)) {
            return advance();
        }

        push_error(std::move(message), current_span());
        return std::nullopt;
    }

    auto push_error(std::string message, Span span) noexcept -> void {
        result.errors.emplace_back(std::move(message), span);
    }

    constexpr auto is_top_level_start(Token token) const noexcept -> bool {
        return is_keyword(token, "import")
            || is_keyword(token, "enum")
            || is_keyword(token, "struct")
            || is_keyword(token, "fn");
    }

    auto synchronize_top_level() noexcept -> void {
        while (!eof()) {
            const auto* token = peek();
            if (token == nullptr || is_top_level_start(*token)) return;

            if (token->kind == TokenKind::SemiColon || token->kind == TokenKind::RightBrace) {
                advance();
                return;
            }

            advance();
        }
    }

    auto synchronize_to_item_end() noexcept -> void {
        auto depth = 0uz;
        while (!eof()) {
            const auto* token = peek();
            if (token == nullptr) return;

            if (depth == 0 && is_top_level_start(*token)) return;

            const auto current_token = advance();
            if (!current_token) return;

            if (current_token->kind == TokenKind::LeftBrace) {
                ++depth;
            } else if (current_token->kind == TokenKind::RightBrace) {
                if (depth == 0) return;
                --depth;
                if (depth == 0) return;
            } else if (depth == 0 && current_token->kind == TokenKind::SemiColon) {
                return;
            }
        }
    }

    auto parse_import() noexcept -> std::optional<ImportItem> {
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

    auto parse_enum() noexcept -> std::optional<EnumItem> {
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

    auto parse_struct() noexcept -> std::optional<StructItem> {
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

    auto parse_params() noexcept -> std::optional<std::vector<FunctionParam>> {
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

    auto parse_function() noexcept -> std::optional<FunctionItem> {
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

        auto return_type = Span {};
        if (match(TokenKind::Arrow)) {
            const auto type = expect_type("expected function return type");
            if (!type) {
                synchronize_to_item_end();
                return std::nullopt;
            }
            return_type = type->span;
        }

        const auto lbrace = expect(TokenKind::LeftBrace, "expected '{' before function body");
        if (!lbrace) {
            synchronize_to_item_end();
            return std::nullopt;
        }

        auto depth = 1uz;
        while (!eof()) {
            const auto token = advance();
            if (!token) break;

            if (token->kind == TokenKind::LeftBrace) {
                ++depth;
            } else if (token->kind == TokenKind::RightBrace) {
                --depth;
                if (depth == 0) {
                    return FunctionItem {
                        .name = name->span,
                        .params = std::move(*params),
                        .return_type = return_type,
                        .body_span = { .start = lbrace->span.end, .end = token->span.start }
                    };
                }
            }
        }

        push_error("expected '}' after function body", eof_span());
        return std::nullopt;
    }
};

export auto parse(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult {
    return Parser(tokens, source).parse();
}
