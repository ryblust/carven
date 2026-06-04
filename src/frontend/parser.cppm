export module carven.frontend.parser;

import carven.common.source;
import carven.frontend.token;
import carven.frontend.ast;
import std;

export struct ParseError final {
    std::string message;
    Span span;
    SourceLocation location;
};

template<> struct std::formatter<ParseError> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }

    auto format(const ParseError& error, auto&& context) const noexcept {
        return std::format_to(context.out(), "error:{}: {}", error.location, error.message);
    }
};

export struct ParseResult final {
    std::vector<TopLevelItem> items;
    std::vector<ParseError> errors;
    Arena storage;
};

class Parser final {
public:
    constexpr Parser(std::span<const Token> tokens, const SourceFile& file) noexcept : tokens(tokens), source(file) {}

    constexpr auto parse() noexcept -> ParseResult {
        while (!eof()) {
            const auto token = peek();
            if (token == nullptr) break;

            switch (token->kind) {
                using enum TokenKind;
                case Import: parse_import();   break;
                case Enum:   parse_enum();     break;
                case Struct: parse_struct();   break;
                case Fn:     parse_function(); break;
                default:
                    push_error("expected top-level item", token->span);
                    advance();
                    synchronize_top_level();
                    break;
            }
        }

        result.storage = std::move(arena);
        return std::move(result);
    }

private:
    std::span<const Token> tokens;
    const SourceFile& source;
    std::size_t current = 0;
    Arena arena;
    ParseResult result;

    enum class PrecedenceLevel : std::uint32_t {
        LogicalOr, LogicalAnd, BitwiseOr, BitwiseXor, BitwiseAnd, Equality, Relational, Shift, Additive, Multiplicative,
    };

    static constexpr auto compound_assign_op(TokenKind kind) noexcept -> std::optional<BinOp> {
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

    static constexpr auto token_to_binop(TokenKind kind, PrecedenceLevel level) noexcept -> std::optional<BinOp> {
        using enum TokenKind;
        using enum BinOp;

        switch (level) {
            case PrecedenceLevel::LogicalOr:
                if (kind == PipePipe) return LogicalOr;
                return std::nullopt;
            case PrecedenceLevel::LogicalAnd:
                if (kind == AmpersandAmpersand) return LogicalAnd;
                return std::nullopt;
            case PrecedenceLevel::BitwiseOr:
                if (kind == Pipe) return BitOr;
                return std::nullopt;
            case PrecedenceLevel::BitwiseXor:
                if (kind == Caret) return BitXor;
                return std::nullopt;
            case PrecedenceLevel::BitwiseAnd:
                if (kind == Ampersand) return BitAnd;
                return std::nullopt;
            case PrecedenceLevel::Equality:
                if (kind == EqualEqual) return Eq;
                if (kind == BangEqual)  return Ne;
                return std::nullopt;
            case PrecedenceLevel::Relational:
                if (kind == Less)          return Lt;
                if (kind == LessEqual)     return Le;
                if (kind == Greater)       return Gt;
                if (kind == GreaterEqual)  return Ge;
                return std::nullopt;
            case PrecedenceLevel::Shift:
                if (kind == LeftShift)  return Shl;
                if (kind == RightShift) return Shr;
                return std::nullopt;
            case PrecedenceLevel::Additive:
                if (kind == Plus)  return Add;
                if (kind == Minus) return Sub;
                return std::nullopt;
            case PrecedenceLevel::Multiplicative:
                if (kind == Star)     return Mul;
                if (kind == Slash)    return Div;
                if (kind == Percent)  return Mod;
                return std::nullopt;
        }

        return std::nullopt;
    }

    constexpr auto eof() const noexcept -> bool {
        return current >= tokens.size();
    }

    constexpr auto eof_span() const noexcept -> Span {
        const auto pos = static_cast<std::uint32_t>(source.text().size());
        return { .start = pos, .end = pos };
    }

    constexpr auto current_span() const noexcept -> Span {
        if (const auto token = peek()) return token->span;

        return eof_span();
    }

    constexpr auto peek(std::size_t offset = 0) const noexcept -> const Token* {
        const auto index = current + offset;
        return index < tokens.size() ? &tokens[index] : nullptr;
    }

    constexpr auto advance() noexcept -> const Token* {
        if (eof()) return nullptr;
        return &tokens[current++];
    }

    constexpr auto check(TokenKind kind) const noexcept -> bool {
        const auto token = peek();
        return token != nullptr && token->kind == kind;
    }

    constexpr auto match(TokenKind kind) noexcept -> const Token* {
        if (!check(kind)) return nullptr;
        return advance();
    }


    constexpr auto expect(TokenKind kind, std::string_view message) noexcept -> const Token* {
        if (const auto token = match(kind)) return token;
        push_error(message, current_span());
        return nullptr;
    }

    constexpr auto expect_name(std::string_view message) noexcept -> const Token* {
        return expect(TokenKind::Identifier, message);
    }

    constexpr auto parse_type_annotation(std::string_view message) noexcept -> std::optional<Span> {
        const auto start = peek();
        if (start == nullptr) {
            push_error(message, eof_span());
            return std::nullopt;
        }

        if (start->kind == TokenKind::LeftBracket) {
            const auto lbracket = advance()->span;
            const auto inner = expect_name("expected array element type after '['");
            if (!inner) return std::nullopt;
            if (!expect(TokenKind::SemiColon, "expected ';' in array type")) return std::nullopt;
            const auto size_expr = parse_expr();
            if (!size_expr) {
                push_error("expected array size expression", current_span());
                return std::nullopt;
            }
            const auto rbracket = expect(TokenKind::RightBracket, "expected ']' after array size");
            if (!rbracket) return std::nullopt;
            return Span{lbracket.start, rbracket->span.end};
        }

        if (start->kind == TokenKind::Identifier) {
            const auto span = advance()->span;
            return span;
        }

        push_error(message, current_span());
        return std::nullopt;
    }

    constexpr auto push_error(std::string_view message, Span span) noexcept -> void {
        result.errors.emplace_back(std::string(message), span, source.location(span.start));
    }

    constexpr auto is_top_level_start(Token token) const noexcept -> bool {
        return token.kind == TokenKind::Import
            || token.kind == TokenKind::Enum
            || token.kind == TokenKind::Struct
            || token.kind == TokenKind::Fn;
    }

    constexpr auto synchronize_top_level() noexcept -> void {
        while (!eof()) {
            const auto token = peek();

            if (token == nullptr || is_top_level_start(*token)) {
                return;
            }

            if (token->kind == TokenKind::SemiColon || token->kind == TokenKind::RightBrace) {
                advance();
                return;
            }

            advance();
        }
    }

    constexpr auto synchronize_to_item_end() noexcept -> void {
        auto depth = 0uz;
        while (!eof()) {
            const auto token = peek();

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

    constexpr auto synchronize_to_stmt_end() noexcept -> void {
        while (!eof()) {
            const auto token = peek();
            if (token == nullptr) return;
            if (token->kind == TokenKind::SemiColon || token->kind == TokenKind::RightBrace) {
                advance();
                return;
            }
            if (is_top_level_start(*token)) return;
            advance();
        }
    }

    template<typename T, typename... Args>
    constexpr auto alloc(Args&&... args) noexcept -> T* {
        return arena.alloc<T>(std::forward<Args>(args)...);
    }

    constexpr auto parse_expr() noexcept -> Expr* {
        const auto lhs = parse_assignment_expr();

        if (check(TokenKind::Comma)) {
            const auto comma_token = advance();
            const auto rhs = parse_expr();
            return alloc<CommaExpr>(lhs, comma_token->span, rhs);
        }

        return lhs;
    }

    constexpr auto parse_block() noexcept -> BlockStmt* {
        const auto lbrace = expect(TokenKind::LeftBrace, "expected '{'");
        if (!lbrace) return nullptr;
        auto statements = std::vector<Stmt*>();
        while (!eof() && !check(TokenKind::RightBrace)) {
            if (auto stmt = parse_stmt()) {
                statements.emplace_back(stmt);
            }
        }

        const auto rbrace = expect(TokenKind::RightBrace, "expected '}'");
        if (!rbrace) return nullptr;
        return alloc<BlockStmt>(lbrace->span, std::move(statements), rbrace->span);
    }

    constexpr auto parse_stmt() noexcept -> Stmt* {
        const auto token = peek();

        if (token == nullptr) {
            return nullptr;
        }

        if (token->kind == TokenKind::SemiColon) {
            const auto span = advance()->span;
            return alloc<EmptyStmt>(span);
        }

        switch (token->kind) {
            using enum TokenKind;
            case Let:
            case Var:
            case Const:  return parse_var_decl();
            case Return: return parse_return_stmt();
            case While:  return parse_while_stmt();
            case For:    return parse_for_stmt();
            case If: {
                const auto expr = parse_if_expr();
                return alloc<ExprStmt>(expr, Span{});
            }
            default: break;
        }

        return parse_expr_stmt();
    }

    constexpr auto parse_expr_stmt() noexcept -> Stmt* {
        const auto expr = parse_expr();
        if (const auto semi = match(TokenKind::SemiColon)) {
            return alloc<ExprStmt>(expr, semi->span);
        }
        if (check(TokenKind::RightBrace) || eof()) {
            return alloc<ExprStmt>(expr, Span{});
        }
        push_error("expected ';' after expression", current_span());
        return alloc<EmptyStmt>(eof_span());
    }

    constexpr auto parse_var_decl_data() noexcept -> VarDecl* {
        const auto keyword = advance();
        const auto name = expect_name("expected variable name");
        if (!name) {
            synchronize_to_stmt_end();
            return nullptr;
        }
        auto type_span = Span{};
        if (check(TokenKind::Colon)) {
            advance();
            const auto type = parse_type_annotation("expected type after ':'");
            if (!type) {
                synchronize_to_stmt_end();
                return nullptr;
            }
            type_span = *type;
        }
        auto eq_span = Span{};
        Expr* init = nullptr;
        if (check(TokenKind::Equal)) {
            eq_span = advance()->span;
            init = parse_expr();
        }

        const auto keyword_text = source.slice(keyword->span);
        if ((keyword_text == "let" || keyword_text == "const") && init == nullptr) {
            push_error(std::format("{} requires an initializer", keyword_text), keyword->span);
        }
        return alloc<VarDecl>(keyword->span, name->span, type_span, eq_span, Span{}, init);
    }

    constexpr auto parse_var_decl() noexcept -> Stmt* {
        auto decl = parse_var_decl_data();
        if (!decl) return nullptr;
        const auto semi = expect(TokenKind::SemiColon, "expected ';' after variable declaration");
        if (!semi) {
            synchronize_to_stmt_end();
            return nullptr;
        }
        decl->semicolon = semi->span;
        return decl;
    }

    constexpr auto parse_return_stmt() noexcept -> Stmt* {
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
        return alloc<ReturnStmt>(keyword->span, semi->span, value);
    }

    constexpr auto parse_stmt_or_block() noexcept -> Stmt* {
        if (check(TokenKind::LeftBrace)) {
            return parse_block();
        }
        return parse_stmt();
    }

    constexpr auto parse_while_stmt() noexcept -> Stmt* {
        const auto keyword = advance();
        const auto condition = parse_expr();

        if (const auto body = parse_stmt_or_block()) {
            return alloc<WhileStmt>(keyword->span, condition, body);
        }

        return nullptr;
    }

    constexpr auto parse_for_stmt() noexcept -> Stmt* {
        const auto keyword = advance();
        const auto lparen = expect(TokenKind::LeftParen, "expected '(' after for");
        if (!lparen) { synchronize_to_stmt_end(); return nullptr; }

        std::variant<VarDecl*, ExprStmt*> for_init;

        if (!check(TokenKind::SemiColon)) {
            const auto token = peek();
            if (token != nullptr && (
                token->kind == TokenKind::Let ||
                token->kind == TokenKind::Var ||
                token->kind == TokenKind::Const
            )) {
                if (auto decl = parse_var_decl_data()) {
                    for_init = decl;
                }
            } else {
                const auto expr = parse_expr();
                for_init = alloc<ExprStmt>(expr, Span{});
            }
        }

        const auto init_semi = expect(TokenKind::SemiColon, "expected ';' after for-init");
        if (!init_semi) { synchronize_to_stmt_end(); return nullptr; }
        const auto init_semi_span = init_semi->span;

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
            return alloc<ForStmt>(keyword->span, lparen->span, for_init, init_semi_span,
                                  cond_semi->span, condition, step, rparen->span, body);
        }

        return nullptr;
    }

    constexpr auto parse_assignment_expr() noexcept -> Expr* {
        const auto lhs = parse_binary_expr(PrecedenceLevel::LogicalOr);
        if (check(TokenKind::Equal)) {
            const auto eq_token = advance();
            const auto rhs = parse_assignment_expr();
            return alloc<AssignExpr>(lhs, eq_token->span, rhs);
        }
        const auto next = peek();
        if (next != nullptr) {
            if (const auto op = compound_assign_op(next->kind)) {
                const auto op_token = advance();
                const auto rhs = parse_assignment_expr();
                return alloc<CompoundAssignExpr>(lhs, *op, op_token->span, rhs);
            }
        }
        return lhs;
    }

    constexpr auto parse_binary_expr(PrecedenceLevel level) noexcept -> Expr* {
        auto lhs = parse_next_tighter(level);
        while (true) {
            const auto token = peek();
            if (token == nullptr) break;
            const auto op = token_to_binop(token->kind, level);
            if (!op) break;
            const auto span = token->span;
            advance();
            const auto rhs = parse_next_tighter(level);
            lhs = alloc<BinaryExpr>(lhs, *op, span, rhs);
        }
        return lhs;
    }

    constexpr auto parse_next_tighter(PrecedenceLevel level) noexcept -> Expr* {
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

    constexpr auto parse_prefix_expr() noexcept -> Expr* {
        const auto token = peek();
        if (token == nullptr) {
            return alloc<LiteralExpr>(eof_span());
        }

        switch (token->kind) {
            using enum TokenKind;
            case Bang: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::LogicalNot, op_span, parse_prefix_expr());
            }
            case Minus: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::Neg, op_span, parse_prefix_expr());
            }
            case Tilde: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::BitNot, op_span, parse_prefix_expr());
            }
            case PlusPlus: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::PreInc, op_span, parse_prefix_expr());
            }
            case MinusMinus: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::PreDec, op_span, parse_prefix_expr());
            }
            case Ampersand: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::AddressOf, op_span, parse_prefix_expr());
            }
            case Star: {
                const auto op_span = advance()->span;
                return alloc<PrefixExpr>(UnaryOp::Deref, op_span, parse_prefix_expr());
            }
            default: return parse_postfix_expr();
        }
    }

    constexpr auto parse_postfix_expr() noexcept -> Expr* {
        auto expr = parse_primary_expr();
        return parse_postfix_ops(expr);
    }

    constexpr auto parse_postfix_ops(Expr* lhs) noexcept -> Expr* {
        while (true) {
            const auto token = peek();
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
                        lhs = alloc<LiteralExpr>(lparen);
                        break;
                    }
                    lhs = alloc<CallExpr>(lhs, lparen, std::move(args), rparen->span);
                    break;
                }
                case LeftBracket: {
                    const auto lbracket = advance()->span;
                    const auto index = parse_expr();
                    const auto rbracket = expect(RightBracket, "expected ']' after index");
                    if (!rbracket) {
                        lhs = alloc<LiteralExpr>(lbracket);
                        break;
                    }
                    lhs = alloc<IndexExpr>(lhs, lbracket, index, rbracket->span);
                    break;
                }
                case Dot:
                case Arrow:
                case ColonColon: {
                    const auto op_span = advance()->span;
                    const auto field_name = expect_name(std::format("expected field name after '{}'", source.slice(op_span)));
                    if (!field_name) {
                        lhs = alloc<LiteralExpr>(op_span);
                        break;
                    }
                    lhs = alloc<FieldExpr>(lhs, op_span, field_name->span);
                    break;
                }
                case PlusPlus: {
                    const auto op_span = advance()->span;
                    lhs = alloc<PostfixExpr>(lhs, UnaryOp::PostInc, op_span);
                    break;
                }
                case MinusMinus: {
                    const auto op_span = advance()->span;
                    lhs = alloc<PostfixExpr>(lhs, UnaryOp::PostDec, op_span);
                    break;
                }
                default: return lhs;
            }
        }
        return lhs;
    }

    constexpr auto parse_primary_expr() noexcept -> Expr* {
        const auto token = peek();
        if (token == nullptr) {
            return alloc<LiteralExpr>(eof_span());
        }

        switch (token->kind) {
            using enum TokenKind;
            case NumberLiteral:
            case StringLiteral:
            case CharLiteral: {
                const auto span = advance()->span;
                return alloc<LiteralExpr>(span);
            }
            case True:
            case False: {
                const auto span = advance()->span;
                return alloc<LiteralExpr>(span);
            }
            case If: return parse_if_expr();
            case Match: return parse_match_expr();
            case Enum:
            case Struct:
            case Fn:
            case Var:
            case Let:
            case Const:
            case As:
            case While:
            case For:
            case Return:
            case Import:
            case Export:
            case Using:
            case Else: {
                const auto keyword = source.slice(token->span);
                push_error(std::format("keyword '{}' cannot be used as an identifier", keyword), token->span);
                advance();
                return alloc<LiteralExpr>(token->span);
            }
            case Identifier: {
                const auto span = advance()->span;
                return alloc<IdentExpr>(span);
            }
            case LeftParen: {
                const auto lparen = advance()->span;
                const auto inner = parse_expr();
                const auto rparen = expect(RightParen, "expected ')' after grouped expression");
                if (!rparen) {
                    return alloc<LiteralExpr>(lparen);
                }
                return alloc<GroupExpr>(lparen, inner, rparen->span);
            }
            case LeftBracket: {
                const auto lbracket = advance()->span;
                auto elements = std::vector<Expr*>();
                if (!check(RightBracket)) {
                    elements.emplace_back(parse_assignment_expr());
                    while (check(Comma)) {
                        advance();
                        if (check(RightBracket)) break; // trailing comma
                        elements.emplace_back(parse_assignment_expr());
                    }
                }
                const auto rbracket = expect(RightBracket, "expected ']' after array elements");
                if (!rbracket) {
                    return alloc<LiteralExpr>(lbracket);
                }
                return alloc<ArrayExpr>(lbracket, std::move(elements), rbracket->span);
            }
            default: {
                const auto span = token->span;
                push_error("unexpected token in expression", span);
                advance();
                return alloc<LiteralExpr>(span);
            }
        }
    }

    constexpr auto parse_match_pattern() noexcept -> std::optional<MatchArm> {
        const auto start = peek();
        if (start == nullptr) return std::nullopt;

        auto arm = MatchArm{};

        if (start->kind == TokenKind::Identifier && source.slice(start->span) == "_") {
            advance();
            arm.is_wildcard = true;
        } else if (start->kind == TokenKind::NumberLiteral
                || start->kind == TokenKind::StringLiteral
                || start->kind == TokenKind::CharLiteral
                || start->kind == TokenKind::True
                || start->kind == TokenKind::False) {
            arm.patterns.emplace_back(parse_primary_expr());
            while (check(TokenKind::Pipe)) {
                advance();
                const auto next = peek();
                if (next == nullptr || !(next->kind == TokenKind::NumberLiteral
                    || next->kind == TokenKind::StringLiteral
                    || next->kind == TokenKind::CharLiteral
                    || next->kind == TokenKind::True
                    || next->kind == TokenKind::False)) {
                    push_error("expected literal pattern after '|'", current_span());
                    return std::nullopt;
                }
                arm.patterns.emplace_back(parse_primary_expr());
            }
        } else if (start->kind == TokenKind::Identifier) {
            arm.patterns.emplace_back(alloc<IdentExpr>(start->span));
            advance();
            while (check(TokenKind::Pipe)) {
                advance();
                const auto next = peek();
                if (next == nullptr || next->kind != TokenKind::Identifier) {
                    push_error("expected type pattern after '|'", current_span());
                    return std::nullopt;
                }
                arm.patterns.emplace_back(alloc<IdentExpr>(next->span));
                advance();
            }
        } else {
            push_error("expected match pattern (literal, type name, or _)", start->span);
            return std::nullopt;
        }

        const auto arrow = expect(TokenKind::FatArrow, "expected '=>' after match pattern");
        if (!arrow) return std::nullopt;
        arm.arrow = arrow->span;

        auto body = parse_block();
        if (!body) {
            push_error("expected block body after '=>'", current_span());
            return std::nullopt;
        }
        arm.body = body;

        return arm;
    }

    constexpr auto parse_match_expr() noexcept -> Expr* {
        const auto keyword = advance();
        const auto value = parse_expr();
        if (!value) {
            return alloc<LiteralExpr>(keyword->span);
        }

        const auto lbrace = expect(TokenKind::LeftBrace, "expected '{' after match expression");
        if (!lbrace) {
            return alloc<LiteralExpr>(keyword->span);
        }

        auto arms = std::vector<MatchArm>();
        while (!eof() && !check(TokenKind::RightBrace)) {
            auto arm = parse_match_pattern();
            if (!arm) {
                // skip to next arm or closing brace
                while (!eof() && !check(TokenKind::RightBrace) && !check(TokenKind::FatArrow)) {
                    advance();
                }
                // advance past the FatArrow we stopped at (if any) so we don't loop forever
                if (check(TokenKind::FatArrow)) advance();
                continue;
            }
            arms.emplace_back(std::move(*arm));

            // optional comma between arms
            if (check(TokenKind::Comma)) advance();
        }

        const auto rbracket = expect(TokenKind::RightBrace, "expected '}' after match arms");
        if (!rbracket) {
            return alloc<LiteralExpr>(lbrace->span);
        }

        return alloc<MatchExpr>(keyword->span, value, lbrace->span, std::move(arms), rbracket->span);
    }

    constexpr auto parse_if_expr() noexcept -> Expr* {
        const auto keyword = advance();
        const auto condition = parse_expr();
        if (!check(TokenKind::LeftBrace)) {
            push_error("expected '{' after if condition", current_span());
            return alloc<LiteralExpr>(keyword->span);
        }
        auto then_branch = parse_block();
        if (!then_branch) {
            return alloc<LiteralExpr>(keyword->span);
        }
        Span else_kw;
        BlockStmt* else_branch = nullptr;
        if (const auto next = peek(); next != nullptr && next->kind == TokenKind::Else) {
            else_kw = advance()->span;
            if (!check(TokenKind::LeftBrace)) {
                push_error("expected '{' after else", current_span());
                return alloc<LiteralExpr>(keyword->span);
            }
            auto parsed_else = parse_block();
            if (!parsed_else) {
                return alloc<LiteralExpr>(keyword->span);
            }
            else_branch = parsed_else;
        }

        return alloc<IfExpr>(keyword->span, condition, then_branch, else_kw, else_branch);
    }

    constexpr auto parse_import() noexcept -> void {
        advance();
        const auto module_name = expect_name("expected import module name");
        if (!module_name) {
            synchronize_to_item_end();
            return;
        }

        auto item = ImportItem {
            .module_name = module_name->span,
            .using_decls = {},
            .is_std_module = source.slice(module_name->span) == "std"
        };

        if (match(TokenKind::Using)) {
            if (match(TokenKind::Star)) {
                item.using_wildcard = true;
            } else if (match(TokenKind::LeftBrace)) {
                while (!eof() && !check(TokenKind::RightBrace)) {
                    const auto decl = expect_name("expected using declaration name");
                    if (decl) item.using_decls.emplace_back(decl->span);

                    if (!match(TokenKind::Comma) && !check(TokenKind::RightBrace)) {
                        push_error("expected ',' or '}' in using declaration list", current_span());
                        synchronize_to_item_end();
                        return;
                    }
                }

                if (!expect(TokenKind::RightBrace, "expected '}' after using declaration list")) {
                    synchronize_to_item_end();
                    return;
                }
            } else {
                const auto decl = expect_name("expected using declaration name");
                if (!decl) {
                    synchronize_to_item_end();
                    return;
                }
                item.using_decls.emplace_back(decl->span);
            }
        }

        match(TokenKind::SemiColon);
        result.items.emplace_back(std::move(item));
    }

    constexpr auto parse_enum() noexcept -> void {
        advance();
        const auto name = expect_name("expected enum name");
        if (!name) {
            synchronize_to_item_end();
            return;
        }

        auto item = EnumItem {
            .name = name->span,
            .size = {},
            .fields = {}
        };

        if (match(TokenKind::Colon)) {
            const auto size = parse_type_annotation("expected enum size type");
            if (!size) {
                synchronize_to_item_end();
                return;
            }
            item.size = *size;
        }

        if (!expect(TokenKind::LeftBrace, "expected '{' before enum fields")) {
            synchronize_to_item_end();
            return;
        }

        while (!eof() && !check(TokenKind::RightBrace)) {
            const auto field = expect_name("expected enum field name");
            if (field) item.fields.emplace_back(field->span);

            if (match(TokenKind::Comma)) continue;
            if (!check(TokenKind::RightBrace)) {
                push_error("expected ',' or '}' after enum field", current_span());
                synchronize_to_item_end();
                return;
            }
        }

        if (!expect(TokenKind::RightBrace, "expected '}' after enum fields")) {
            synchronize_to_item_end();
            return;
        }

        result.items.emplace_back(std::move(item));
    }

    constexpr auto parse_struct() noexcept -> void {
        advance();
        const auto name = expect_name("expected struct name");
        if (!name) {
            synchronize_to_item_end();
            return;
        }

        auto item = StructItem {
            .name = name->span,
            .fields = {}
        };

        if (!expect(TokenKind::LeftBrace, "expected '{' before struct fields")) {
            synchronize_to_item_end();
            return;
        }

        while (!eof() && !check(TokenKind::RightBrace)) {
            const auto field_name = expect_name("expected struct field name");
            if (!field_name) {
                synchronize_to_item_end();
                return;
            }

            if (!expect(TokenKind::Colon, "expected ':' after struct field name")) {
                synchronize_to_item_end();
                return;
            }

            const auto field_type = parse_type_annotation("expected struct field type");
            if (!field_type) {
                synchronize_to_item_end();
                return;
            }

            item.fields.emplace_back(field_name->span, *field_type);

            if (match(TokenKind::Comma) || match(TokenKind::SemiColon)) {
                continue;
            }

            if (!check(TokenKind::RightBrace)) {
                push_error("expected ',' or '}' after struct field", current_span());
                synchronize_to_item_end();
                return;
            }
        }

        if (!expect(TokenKind::RightBrace, "expected '}' after struct fields")) {
            synchronize_to_item_end();
            return;
        }

        result.items.emplace_back(std::move(item));
    }

    constexpr auto parse_params() noexcept -> std::optional<std::vector<FunctionParam>> {
        auto params = std::vector<FunctionParam>();
        while (!eof() && !check(TokenKind::RightParen)) {
            const auto param_name = expect_name("expected function parameter name");
            if (!param_name) return std::nullopt;

            auto type_span = Span{};
            if (match(TokenKind::Colon)) {
                const auto param_type = parse_type_annotation("expected function parameter type");
                if (!param_type) return std::nullopt;
                type_span = *param_type;
            }

            params.emplace_back(param_name->span, type_span);

            if (match(TokenKind::Comma)) continue;
            if (!check(TokenKind::RightParen)) {
                push_error("expected ',' or ')' after function parameter", current_span());
                return std::nullopt;
            }
        }

        return params;
    }

    constexpr auto parse_function() noexcept -> void {
        advance();
        const auto name = expect_name("expected function name");
        if (!name) {
            synchronize_to_item_end();
            return;
        }
        if (!expect(TokenKind::LeftParen, "expected '(' after function name")) {
            synchronize_to_item_end();
            return;
        }
        auto params = parse_params();
        if (!params) {
            synchronize_to_item_end();
            return;
        }
        if (!expect(TokenKind::RightParen, "expected ')' after function parameters")) {
            synchronize_to_item_end();
            return;
        }
        auto return_type = Span{};
        if (match(TokenKind::Arrow)) {
            const auto type = parse_type_annotation("expected function return type");
            if (!type) {
                synchronize_to_item_end();
                return;
            }
            return_type = *type;
        }
        auto block = parse_block();
        if (!block) {
            synchronize_to_item_end();
            return;
        }

        result.items.emplace_back(FunctionItem {
            .name = name->span,
            .params = std::move(*params),
            .return_type = return_type,
            .body = block
        });
    }
};

export constexpr auto parse(std::span<const Token> tokens, const SourceFile& source) noexcept -> ParseResult {
    return Parser(tokens, source).parse();
}
