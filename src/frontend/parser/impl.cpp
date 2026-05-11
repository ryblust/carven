module zero.frontend.parser:impl;

import zero.common.source;
import zero.frontend.lexer.token;
import zero.frontend.parser.ast;
import std;

enum class PrecedenceLevel {
    LogicalOr, LogicalAnd, BitwiseOr, BitwiseXor, BitwiseAnd,
    Equality, Relational, Shift, Additive, Multiplicative,
};

auto compound_assign_op(TokenKind kind) noexcept -> std::optional<BinOp> {
    using enum TokenKind;
    using enum BinOp;
    switch (kind) {
        case PlusEqual:       return Add;
        case MinusEqual:      return Sub;
        case StarEqual:       return Mul;
        case SlashEqual:      return Div;
        case PercentEqual:    return Mod;
        case AmpersandEqual:  return BitAnd;
        case PipeEqual:       return BitOr;
        case CaretEqual:      return BitXor;
        case LeftShiftEqual:  return Shl;
        case RightShiftEqual: return Shr;
        default:              return std::nullopt;
    }
}

auto token_to_binop(TokenKind kind, PrecedenceLevel level) noexcept -> std::optional<BinOp> {
    using enum TokenKind;
    using enum BinOp;
    switch (level) {
        case PrecedenceLevel::LogicalOr: {
            if (kind == PipePipe) return LogicalOr;
            return std::nullopt;
        }
        case PrecedenceLevel::LogicalAnd: {
            if (kind == AmpersandAmpersand) return LogicalAnd;
            return std::nullopt;
        }
        case PrecedenceLevel::BitwiseOr: {
            if (kind == Pipe) return BitOr;
            return std::nullopt;
        }
        case PrecedenceLevel::BitwiseXor: {
            if (kind == Caret) return BitXor;
            return std::nullopt;
        }
        case PrecedenceLevel::BitwiseAnd: {
            if (kind == Ampersand) return BitAnd;
            return std::nullopt;
        }
        case PrecedenceLevel::Equality: {
            if (kind == EqualEqual) return Eq;
            if (kind == BangEqual)  return Ne;
            return std::nullopt;
        }
        case PrecedenceLevel::Relational: {
            if (kind == Less)          return Lt;
            if (kind == LessEqual)     return Le;
            if (kind == Greater)       return Gt;
            if (kind == GreaterEqual)  return Ge;
            return std::nullopt;
        }
        case PrecedenceLevel::Shift: {
            if (kind == LeftShift)  return Shl;
            if (kind == RightShift) return Shr;
            return std::nullopt;
        }
        case PrecedenceLevel::Additive: {
            if (kind == Plus)  return Add;
            if (kind == Minus) return Sub;
            return std::nullopt;
        }
        case PrecedenceLevel::Multiplicative: {
            if (kind == Star)     return Mul;
            if (kind == Slash)    return Div;
            if (kind == Percent)  return Mod;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

class Parser final {
public:
    Parser(std::span<const Token> t, std::string_view s) noexcept : tokens(t), source(s) {}

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

    auto parse_expr() noexcept -> Expr* {
        const auto lhs = parse_assignment_expr();
        if (check(TokenKind::Comma)) {
            const auto comma_tok = advance();
            const auto rhs = parse_expr();
            return make_expr(CommaExpr{ .lhs = lhs, .comma = comma_tok->span, .rhs = rhs });
        }
        return lhs;
    }

    auto take_result() noexcept -> ParseResult { return std::move(result); }

    auto parse_block() noexcept -> std::optional<BlockStmt>;

    auto parse_stmt() noexcept -> Stmt*;
    auto parse_expr_stmt() noexcept -> Stmt*;
    auto parse_var_decl() noexcept -> Stmt*;
    auto parse_return_stmt() noexcept -> Stmt*;
    auto parse_while_stmt() noexcept -> Stmt*;
    auto parse_for_stmt() noexcept -> Stmt*;
    auto parse_stmt_or_block() noexcept -> Stmt*;

    auto parse_assignment_expr() noexcept -> Expr*;
    auto parse_binary_expr(PrecedenceLevel level) noexcept -> Expr*;
    auto parse_next_tighter(PrecedenceLevel level) noexcept -> Expr*;
    auto parse_prefix_expr() noexcept -> Expr*;
    auto parse_postfix_expr() noexcept -> Expr*;
    auto parse_postfix_ops(Expr* lhs) noexcept -> Expr*;
    auto parse_primary_expr() noexcept -> Expr*;
    auto parse_if_expr() noexcept -> Expr*;

    auto parse_import() noexcept -> std::optional<ImportItem>;
    auto parse_enum() noexcept -> std::optional<EnumItem>;
    auto parse_struct() noexcept -> std::optional<StructItem>;
    auto parse_function() noexcept -> std::optional<FunctionItem>;
    auto parse_params() noexcept -> std::optional<std::vector<FunctionParam>>;

private:
    std::span<const Token> tokens;
    std::string_view source;
    std::size_t current = 0;
    ParseResult result;

    auto eof() const noexcept -> bool {
        return current >= tokens.size();
    }

    auto eof_span() const noexcept -> Span {
        const auto pos = static_cast<std::uint32_t>(source.size());
        return { .start = pos, .end = pos };
    }

    auto current_span() const noexcept -> Span {
        if (const auto* token = peek()) return token->span;
        return eof_span();
    }

    auto peek(std::size_t offset = 0) const noexcept -> const Token* {
        const auto index = current + offset;
        return index < tokens.size() ? &tokens[index] : nullptr;
    }

    auto advance() noexcept -> std::optional<Token> {
        if (eof()) return std::nullopt;
        return tokens[current++];
    }

    auto check(TokenKind kind) const noexcept -> bool {
        const auto* token = peek();
        return token != nullptr && token->kind == kind;
    }

    auto match(TokenKind kind) noexcept -> std::optional<Token> {
        if (!check(kind)) return std::nullopt;
        return advance();
    }

    auto is_keyword(Token token, std::string_view keyword) const noexcept -> bool {
        return token.kind == TokenKind::Keyword && text_at(source, token.span) == keyword;
    }

    auto check_keyword(std::string_view keyword) const noexcept -> bool {
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
        result.errors.emplace_back(std::move(message), span, location_at(source, span.start));
    }

    auto is_top_level_start(Token token) const noexcept -> bool {
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

    auto synchronize_to_stmt_end() noexcept -> void {
        while (!eof()) {
            const auto* tok = peek();
            if (tok == nullptr) return;
            if (tok->kind == TokenKind::SemiColon || tok->kind == TokenKind::RightBrace) {
                advance();
                return;
            }
            if (is_top_level_start(*tok)) return;
            advance();
        }
    }

    auto make_expr(auto&&... args) noexcept -> Expr* {
        auto ptr = std::make_unique<Expr>(std::forward<decltype(args)>(args)...);
        auto* raw = ptr.get();
        result.exprs.emplace_back(std::move(ptr));
        return raw;
    }

    auto make_stmt(auto&&... args) noexcept -> Stmt* {
        auto ptr = std::make_unique<Stmt>(std::forward<decltype(args)>(args)...);
        auto* raw = ptr.get();
        result.stmts.emplace_back(std::move(ptr));
        return raw;
    }

    auto make_for_init(auto&&... args) noexcept -> ForInit* {
        auto ptr = std::make_unique<ForInit>(std::forward<decltype(args)>(args)...);
        auto* raw = ptr.get();
        result.for_inits.emplace_back(std::move(ptr));
        return raw;
    }
};

auto Parser::parse_block() noexcept -> std::optional<BlockStmt> {
    const auto lbrace = expect(TokenKind::LeftBrace, "expected '{'");
    if (!lbrace) return std::nullopt;

    auto statements = std::vector<Stmt*>();

    while (!eof() && !check(TokenKind::RightBrace)) {
        if (auto stmt = parse_stmt()) {
            statements.emplace_back(stmt);
        }
    }

    const auto rbrace = expect(TokenKind::RightBrace, "expected '}'");
    if (!rbrace) return std::nullopt;

    return BlockStmt{
        .lbrace = lbrace->span,
        .statements = std::move(statements),
        .rbrace = rbrace->span
    };
}

auto parse(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult {
    return Parser(tokens, source).parse();
}

auto parse_expr_tokens(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult {
    auto parser = Parser(tokens, source);
    parser.parse_expr();
    return parser.take_result();
}
