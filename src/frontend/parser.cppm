export module zero.frontend.parser;

import zero.common.source;
import zero.frontend.token;
import zero.frontend.ast;
import std;

class Parser final {
public:
    constexpr Parser(std::span<const Token> t, std::string_view s) noexcept : tokens(t), source(s) {}

    constexpr auto parse() noexcept -> ParseResult {
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

    constexpr auto parse_expr() noexcept -> Expr* {
        const auto lhs = parse_assignment_expr();
        if (check(TokenKind::Comma)) {
            const auto comma_tok = advance();
            const auto rhs = parse_expr();
            return make_expr(CommaExpr{ .lhs = lhs, .comma = comma_tok->span, .rhs = rhs });
        }
        return lhs;
    }

    auto take_result() noexcept -> ParseResult { return std::move(result); }

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

    constexpr auto advance() noexcept -> std::optional<Token> {
        if (eof()) return std::nullopt;
        return tokens[current++];
    }

    constexpr auto check(TokenKind kind) const noexcept -> bool {
        const auto* token = peek();
        return token != nullptr && token->kind == kind;
    }

    constexpr auto match(TokenKind kind) noexcept -> std::optional<Token> {
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

    constexpr auto match_keyword(std::string_view keyword) noexcept -> std::optional<Token> {
        if (!check_keyword(keyword)) return std::nullopt;
        return advance();
    }

    constexpr auto expect(TokenKind kind, std::string message) noexcept -> std::optional<Token> {
        if (auto token = match(kind)) return token;
        push_error(std::move(message), current_span());
        return std::nullopt;
    }

    constexpr auto expect_name(std::string message) noexcept -> std::optional<Token> {
        return expect(TokenKind::Identifier, std::move(message));
    }

    constexpr auto expect_type(std::string message) noexcept -> std::optional<Token> {
        const auto* token = peek();
        if (token != nullptr && (token->kind == TokenKind::Identifier || token->kind == TokenKind::Keyword)) {
            return advance();
        }

        push_error(std::move(message), current_span());
        return std::nullopt;
    }

    constexpr auto push_error(std::string message, Span span) noexcept -> void {
        result.errors.emplace_back(std::move(message), span, line_column_at(source, span.start));
    }

    constexpr auto is_top_level_start(Token token) const noexcept -> bool {
        return is_keyword(token, "import")
            || is_keyword(token, "enum")
            || is_keyword(token, "struct")
            || is_keyword(token, "fn");
    }

    constexpr auto synchronize_top_level() noexcept -> void {
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

    constexpr auto synchronize_to_item_end() noexcept -> void {
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

    constexpr auto synchronize_to_stmt_end() noexcept -> void {
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

    constexpr auto make_expr(auto&&... args) noexcept -> Expr* {
        auto ptr = std::make_unique<Expr>(std::forward<decltype(args)>(args)...);
        auto* raw = ptr.get();
        result.exprs.emplace_back(std::move(ptr));
        return raw;
    }

    constexpr auto make_stmt(auto&&... args) noexcept -> Stmt* {
        auto ptr = std::make_unique<Stmt>(std::forward<decltype(args)>(args)...);
        auto* raw = ptr.get();
        result.stmts.emplace_back(std::move(ptr));
        return raw;
    }

    constexpr auto make_for_init(auto&&... args) noexcept -> ForInit* {
        auto ptr = std::make_unique<ForInit>(std::forward<decltype(args)>(args)...);
        auto* raw = ptr.get();
        result.for_inits.emplace_back(std::move(ptr));
        return raw;
    }

    constexpr auto parse_import() noexcept -> std::optional<ImportItem> {
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

    constexpr auto parse_enum() noexcept -> std::optional<EnumItem> {
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

    constexpr auto parse_struct() noexcept -> std::optional<StructItem> {
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

    constexpr auto parse_params() noexcept -> std::optional<std::vector<FunctionParam>> {
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

    constexpr auto parse_function() noexcept -> std::optional<FunctionItem> {
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

    constexpr auto parse_block() noexcept -> std::optional<BlockStmt> {
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

    constexpr auto parse_stmt() noexcept -> Stmt* {
        const auto* tok = peek();
        if (tok == nullptr) return nullptr;

        if (tok->kind == TokenKind::SemiColon) {
            const auto span = advance()->span;
            return make_stmt(EmptyStmt{ span });
        }

        if (tok->kind == TokenKind::Keyword) {
            const auto kw = text_at(source, tok->span);
            if (kw == "let" || kw == "var" || kw == "const") return parse_var_decl();
            if (kw == "return") return parse_return_stmt();
            if (kw == "while") return parse_while_stmt();
            if (kw == "for") return parse_for_stmt();
            if (kw == "if") {
                const auto expr = parse_if_expr();
                return make_stmt(ExprStmt{ .expr = expr, .semicolon = Span{} });
            }
        }

        return parse_expr_stmt();
    }

    constexpr auto parse_expr_stmt() noexcept -> Stmt* {
        const auto expr = parse_expr();
        const auto semi = expect(TokenKind::SemiColon, "expected ';' after expression");
        if (!semi) {
            return make_stmt(EmptyStmt{ expr ? eof_span() : eof_span() });
        }
        return make_stmt(ExprStmt{ .expr = expr, .semicolon= semi->span });
    }

    constexpr auto parse_var_decl() noexcept -> Stmt* {
        const auto kw = advance();
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

        const auto keyword = text_at(source, kw->span);
        if ((keyword == "let" || keyword == "const") && init == nullptr) {
            push_error(std::string(keyword) + " requires an initializer", kw->span);
        }

        const auto semi = expect(TokenKind::SemiColon, "expected ';' after variable declaration");
        if (!semi) {
            synchronize_to_stmt_end();
            return nullptr;
        }

        return make_stmt(VarDecl{
            .keyword = kw->span,
            .name = name->span,
            .type = type_span,
            .eq = eq_span,
            .init = init,
            .semicolon= semi->span
        });
    }

    constexpr auto parse_return_stmt() noexcept -> Stmt* {
        const auto kw = advance();
        Expr* value = nullptr;
        if (!check(TokenKind::SemiColon)) {
            value = parse_expr();
        }
        const auto semi = expect(TokenKind::SemiColon, "expected ';' after return");
        if (!semi) {
            synchronize_to_stmt_end();
            return nullptr;
        }
        return make_stmt(ReturnStmt{ .keyword = kw->span, .value = value, .semicolon= semi->span });
    }

    constexpr auto parse_stmt_or_block() noexcept -> Stmt* {
        if (check(TokenKind::LeftBrace)) {
            if (auto block = parse_block()) return make_stmt(std::move(*block));
            return nullptr;
        }
        return parse_stmt();
    }

    constexpr auto parse_while_stmt() noexcept -> Stmt* {
        const auto kw = advance();
        const auto lparen = expect(TokenKind::LeftParen, "expected '(' after while");
        if (!lparen) { synchronize_to_stmt_end(); return nullptr; }
        const auto condition = parse_expr();
        const auto rparen = expect(TokenKind::RightParen, "expected ')' after while condition");
        if (!rparen) { synchronize_to_stmt_end(); return nullptr; }
        if (const auto body = parse_stmt_or_block()) {
            return make_stmt(WhileStmt{
                .keyword = kw->span, .lparen = lparen->span,
                .condition = condition, .rparen = rparen->span,
                .body = body
            });
        }
        return nullptr;
    }

    constexpr auto parse_for_stmt() noexcept -> Stmt* {
        const auto kw = advance();
        const auto lparen = expect(TokenKind::LeftParen, "expected '(' after for");
        if (!lparen) { synchronize_to_stmt_end(); return nullptr; }

        ForInit* for_init = nullptr;
        if (!check(TokenKind::SemiColon)) {
            const auto* tok = peek();
            if (tok != nullptr && tok->kind == TokenKind::Keyword) {
                const auto kw_text = text_at(source, tok->span);
                if (kw_text == "let" || kw_text == "var" || kw_text == "const") {
                    if (auto decl = parse_var_decl()) {
                        for_init = make_for_init(std::get<VarDecl>(std::move(*decl)));
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
        const auto init_semi = expect(TokenKind::SemiColon, "expected ';' after for-init");
        if (!init_semi) { synchronize_to_stmt_end(); return nullptr; }

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

        const auto body = parse_stmt_or_block();

        return make_stmt(ForStmt{
            .keyword = kw->span, .lparen = lparen->span,
            .init = for_init, .init_semi = init_semi->span,
            .condition = condition, .cond_semi = cond_semi->span,
            .step = step, .rparen = rparen->span, .body = body
        });
    }

    enum class PrecedenceLevel {
        LogicalOr, LogicalAnd, BitwiseOr, BitwiseXor, BitwiseAnd,
        Equality, Relational, Shift, Additive, Multiplicative,
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

    constexpr auto parse_assignment_expr() noexcept -> Expr* {
        // TODO: parse ? : ternary
        const auto lhs = parse_binary_expr(PrecedenceLevel::LogicalOr);

        if (check(TokenKind::Equal)) {
            const auto eq_tok = advance();
            const auto rhs = parse_assignment_expr();
            return make_expr(AssignExpr{ .lhs = lhs, .eq = eq_tok->span, .rhs = rhs });
        }

        const auto* next = peek();
        if (next != nullptr) {
            if (const auto op = compound_assign_op(next->kind)) {
                const auto op_tok = advance();
                const auto rhs = parse_assignment_expr();
                return make_expr(CompoundAssignExpr{ .lhs = lhs, .op = *op, .span = op_tok->span, .rhs = rhs });
            }
        }

        return lhs;
    }

    constexpr auto parse_binary_expr(PrecedenceLevel level) noexcept -> Expr* {
        auto lhs = parse_next_tighter(level);
        while (true) {
            const auto* tok = peek();
            if (tok == nullptr) break;
            const auto op = token_to_binop(tok->kind, level);
            if (!op) break;
            const auto span = tok->span;
            advance();
            const auto rhs = parse_next_tighter(level);
            lhs = make_expr(BinaryExpr{ .lhs = lhs, .op = *op, .span = span, .rhs = rhs });
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
        const auto* tok = peek();
        if (tok == nullptr) {
            return make_expr(LiteralExpr{ eof_span() });
        }

        switch (tok->kind) {
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

    constexpr auto parse_postfix_expr() noexcept -> Expr* {
        auto expr = parse_primary_expr();
        return parse_postfix_ops(expr);
    }

    constexpr auto parse_postfix_ops(Expr* lhs) noexcept -> Expr* {
        while (true) {
            const auto* tok = peek();
            if (tok == nullptr) break;

            switch (tok->kind) {
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

    constexpr auto parse_primary_expr() noexcept -> Expr* {
        const auto* tok = peek();
        if (tok == nullptr) {
            return make_expr(LiteralExpr{ eof_span() });
        }

        switch (tok->kind) {
            using enum TokenKind;
            case NumberLiteral:
            case StringLiteral:
            case CharLiteral: {
                const auto span = advance()->span;
                return make_expr(LiteralExpr{ span });
            }
            case Keyword: {
                const auto keyword = text_at(source, tok->span);
                if (keyword == "true" || keyword == "false") {
                    const auto span = advance()->span;
                    return make_expr(LiteralExpr{ span });
                }
                if (keyword == "if") return parse_if_expr();
                [[fallthrough]];
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
                const auto span = tok->span;
                push_error("unexpected token in expression", span);
                advance();
                return make_expr(LiteralExpr{ span });
            }
        }
    }

    constexpr auto parse_if_expr() noexcept -> Expr* {
        const auto kw = advance();  // consume 'if'

        const auto condition = parse_expr();

        if (!check(TokenKind::LeftBrace)) {
            push_error("expected '{' after if condition", current_span());
            return make_expr(LiteralExpr{ kw->span });
        }

        auto then_branch = parse_block();
        if (!then_branch) {
            return make_expr(LiteralExpr{ kw->span });
        }

        std::optional<Span> else_kw;
        std::optional<BlockStmt> else_branch;

        if (const auto* next = peek(); next != nullptr && is_keyword(*next, "else")) {
            else_kw = advance()->span;

            if (!check(TokenKind::LeftBrace)) {
                push_error("expected '{' after else", current_span());
                return make_expr(LiteralExpr{ kw->span });
            }
            auto parsed_else = parse_block();
            if (!parsed_else) {
                return make_expr(LiteralExpr{ kw->span });
            }
            else_branch = std::move(*parsed_else);
        }

        return make_expr(IfExpr{
            .keyword = kw->span,
            .condition = condition,
            .then_branch = std::move(*then_branch),
            .else_kw = else_kw,
            .else_branch = std::move(else_branch)
        });
    }

};

export constexpr auto parse(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult {
    return Parser(tokens, source).parse();
}

export constexpr auto parse_expr_tokens(std::span<const Token> tokens, std::string_view source) noexcept -> ParseResult {
    auto parser = Parser(tokens, source);
    parser.parse_expr();
    return parser.take_result();
}
