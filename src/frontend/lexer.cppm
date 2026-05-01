export module zero.frontend.lexer;

import zero.frontend.token;
import std;

constexpr auto is_alpha(char c) noexcept -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr auto is_digit(char c) noexcept -> bool {
    return c >= '0' && c <= '9';
}

constexpr auto is_ident(char c) noexcept -> bool {
    return is_alpha(c) || c == '_';
}

constexpr auto is_ident_continue(char c) noexcept -> bool {
    return is_ident(c) || is_digit(c);
}

class Lexer final {
public:
    explicit constexpr Lexer(std::string_view s) noexcept : source(s) {}

    constexpr auto next() noexcept -> Token {
        using enum TokenKind;

        skip_meaningless();

        if (eof()) return record_token(End);

        start_pos = current_pos;

        const auto c = current();
        advance();

        if (is_ident(c)) return identifier_or_keyword();
        if (is_digit(c)) return number_literal();

        switch (c) {
            case '(': return record_token(LeftParen);
            case ')': return record_token(RightParen);
            case '[': return record_token(LeftBracket);
            case ']': return record_token(RightBracket);
            case '{': return record_token(LeftBrace);
            case '}': return record_token(RightBrace);
            case ',': return record_token(Comma);
            case '.': return record_token(Dot);
            case ';': return record_token(SemiColon);
            case ':': return record_token(match(':') ? ColonColon : Colon);

            case '+': return record_token(match('=') ?  PlusEqual : match('+') ? PlusPlus : Plus);
            case '-': return record_token(match('=') ? MinusEqual : match('-') ? MinusMinus : match('>') ? Arrow : Minus);
            case '*': return record_token(match('=') ?  StarEqual :  Star);
            case '/': return record_token(match('=') ? SlashEqual : Slash);
            case '%': return record_token(match('=') ? PercentEqual : Percent);

            case '!': return record_token(match('=') ?  BangEqual : Bang);
            case '=': return record_token(match('=') ? EqualEqual : match('>') ? FatArrow : Equal);

            case '<': return record_token(match('<') ? match('=') ?  LeftShiftEqual :  LeftShift : match('=') ?    LessEqual :    Less);
            case '>': return record_token(match('>') ? match('=') ? RightShiftEqual : RightShift : match('=') ? GreaterEqual : Greater);

            case '&': return record_token(match('&') ? AmpersandAmpersand : match('=') ? AmpersandEqual : Ampersand);
            case '|': return record_token(match('|') ? PipePipe : match('=') ? PipeEqual : Pipe);
            case '^': return record_token(match('=') ? CaretEqual : Caret);
            case '~': return record_token(Tilde);

            case '"':  return string_literal();
            case '\'': return char_literal();

            default:  return record_token(Unknown);
        }
    }
private:
    std::uint32_t current_pos = 0;
    std::uint32_t start_pos = 0;
    std::string_view source;

    constexpr auto advance() noexcept -> void {
        if (!eof()) ++current_pos;
    }

    constexpr auto eof() const noexcept -> bool {
        return current_pos >= source.size();
    }

    constexpr auto current() const noexcept -> char {
        return eof() ? '\0' : source[current_pos];
    }

    constexpr auto match(char expected) noexcept -> bool {
        if (eof() || (current() != expected)) {
            return false;
        }

        advance();

        return true;
    }

    constexpr auto peek(std::size_t offset = 1) const noexcept -> char {
        const auto index = current_pos + offset;

        return index < source.size() ? source[index] : '\0';
    }

    constexpr auto identifier_or_keyword() noexcept -> Token {
        static constexpr auto keywords = std::array {
            "import",
            "export",
            "using",
            "enum",
            "struct",
            "fn",
            "var",
            "let",
            "const",
            "as",
            "if",
            "else",
            "while",
            "for",
            "return",
            "true",
            "false",
        };

        while (is_ident_continue(current())) {
            advance();
        }

        if (std::ranges::contains(keywords, source.substr(start_pos, current_pos - start_pos))) {
            return record_token(TokenKind::Keyword);
        }

        return record_token(TokenKind::Identifier);
    }

    constexpr auto number_literal() noexcept -> Token {
        while (is_digit(current())) {
            advance();
        }

        if (current() == '.' && is_digit(peek())) {
            advance();
            while (is_digit(current())) {
                advance();
            }
        }

        return record_token(TokenKind::NumberLiteral);
    }

    constexpr auto char_literal() noexcept -> Token {
        if (!eof() && current() != '\'' && current() != '\n') {
            if (current() == '\\') {
                advance();
            }
            advance();
        }

        if (!eof() && current() == '\'') {
            advance();
        }

        return record_token(TokenKind::CharLiteral);
    }

    constexpr auto string_literal() noexcept -> Token {
        while (!eof() && current() != '"' && current() != '\n') {
            if (current() == '\\') {
                advance();
            }
            advance();
        }

        if (!eof() && current() == '"') {
            advance();
        }

        return record_token(TokenKind::StringLiteral);
    }

    constexpr auto skip_meaningless() noexcept -> void {
        while (!eof()) {
            switch (current()) {
                case ' ':
                case '\r':
                case '\t':
                case '\n':
                    advance();
                    continue;

                case '/':
                    if (peek() == '/') {
                        advance();
                        advance();

                        while (!eof() && current() != '\n') {
                            advance();
                        }
                        continue;
                    }
                    return;

                default: return;
            }
        }
    }

    constexpr auto record_token(TokenKind kind) const noexcept -> Token {
        return { kind,  { start_pos, current_pos } };
    }
};

export constexpr auto tokenize(std::string_view source) noexcept -> std::vector<Token> {
    auto lexer = Lexer(source);
    auto tokens = std::vector<Token>();
    tokens.reserve(source.size() / 5);

    for (auto t = lexer.next(); t.kind != TokenKind::End; t = lexer.next()) {
        tokens.emplace_back(t);
    }

    return tokens;
}
