export module carven.frontend.lexer;

import carven.frontend.token;
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

constexpr auto is_hex_digit(char c) noexcept -> bool {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr auto is_binary_digit(char c) noexcept -> bool {
    return c == '0' || c == '1';
}

constexpr auto is_octal_digit(char c) noexcept -> bool {
    return c >= '0' && c <= '7';
}

constexpr auto is_valid_escape(char c) noexcept -> bool {
    return c == '\'' || c == '"' || c == '\\' || c == 'n' || c == 't' || c == 'r' || c == '0';
}

class Lexer final {
public:
    explicit constexpr Lexer(std::string_view s) noexcept : source(s) {}

    constexpr auto next() noexcept -> Token {
        using enum TokenKind;
        skip_meaningless();
        if (eof()) return token(End);

        start_pos = current_pos;
        const auto c = current();
        advance();

        if (is_ident(c)) return identifier_or_keyword();
        if (is_digit(c)) return number_literal();

        switch (c) {
            case '(': return token(LeftParen);
            case ')': return token(RightParen);
            case '[': return token(LeftBracket);
            case ']': return token(RightBracket);
            case '{': return token(LeftBrace);
            case '}': return token(RightBrace);
            case ',': return token(Comma);
            case '.': return token(Dot);
            case ';': return token(SemiColon);
            case ':': return token(match(':') ? ColonColon : Colon);

            case '+': return token(match('=') ?    PlusEqual : match('+') ?   PlusPlus : Plus);
            case '-': return token(match('=') ?   MinusEqual : match('-') ? MinusMinus : match('>') ? Arrow : Minus);
            case '*': return token(match('=') ?    StarEqual :    Star);
            case '/': return token(match('=') ?   SlashEqual :   Slash);
            case '%': return token(match('=') ? PercentEqual : Percent);

            case '!': return token(match('=') ?  BangEqual : Bang);
            case '=': return token(match('=') ? EqualEqual : match('>') ? FatArrow : Equal);

            case '<': return token(match('<') ? match('=') ?  LeftShiftEqual :  LeftShift : match('=') ?    LessEqual :    Less);
            case '>': return token(match('>') ? match('=') ? RightShiftEqual : RightShift : match('=') ? GreaterEqual : Greater);

            case '&': return token(match('&') ? AmpersandAmpersand : match('=') ? AmpersandEqual : Ampersand);
            case '|': return token(match('|') ? PipePipe : match('=') ? PipeEqual : Pipe);
            case '^': return token(match('=') ? CaretEqual : Caret);
            case '~': return token(Tilde);

            case '"':  return string_literal();
            case '\'': return char_literal();

            default:  return token(Unknown);
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
        static constexpr auto keywords = std::array<std::pair<std::string_view, TokenKind>, 18> {{
            { "import", TokenKind::Import },
            { "export", TokenKind::Export },
            { "using",  TokenKind::Using  },
            { "fn",     TokenKind::Fn     },
            { "struct", TokenKind::Struct },
            { "enum",   TokenKind::Enum   },
            { "var",    TokenKind::Var    },
            { "let",    TokenKind::Let    },
            { "match",  TokenKind::Match  },
            { "const",  TokenKind::Const  },
            { "if",     TokenKind::If     },
            { "else",   TokenKind::Else   },
            { "for",    TokenKind::For    },
            { "while",  TokenKind::While  },
            { "as",     TokenKind::As     },
            { "true",   TokenKind::True   },
            { "false",  TokenKind::False  },
            { "return", TokenKind::Return },
        }};

        while (is_ident_continue(current())) {
            advance();
        }

        for (const auto text = source.substr(start_pos, current_pos - start_pos); const auto [name, kind] : keywords) {
            if (name == text) return token(kind);
        }

        return token(TokenKind::Identifier);
    }

    constexpr auto number_literal() noexcept -> Token {
        if (source[start_pos] == '0') {
            const auto second = current();
            if (second == 'x' || second == 'X') {
                advance();
                while (is_hex_digit(current())) {
                    advance();
                }
                return token(TokenKind::NumberLiteral);
            }
            if (second == 'b' || second == 'B') {
                advance();
                while (is_binary_digit(current())) {
                    advance();
                }
                return token(TokenKind::NumberLiteral);
            }
            if (second == 'o' || second == 'O') {
                advance();
                while (is_octal_digit(current())) {
                    advance();
                }
                return token(TokenKind::NumberLiteral);
            }
        }

        while (is_digit(current())) {
            advance();
        }

        if (current() == '.' && is_digit(peek())) {
            advance();
            while (is_digit(current())) {
                advance();
            }
        }

        if ((current() == 'e' || current() == 'E')
            && (is_digit(peek()) || ((peek() == '+' || peek() == '-') && is_digit(peek(2))))) {
            advance();
            if (current() == '+' || current() == '-') advance();
            while (is_digit(current())) {
                advance();
            }
        }

        return token(TokenKind::NumberLiteral);
    }

    constexpr auto char_literal() noexcept -> Token {
        auto chars = 0uz;
        auto valid = true;

        while (!eof() && current() != '\'' && current() != '\n') {
            if (current() == '\\') {
                advance();
                if (eof() || current() == '\n') break;
                if (!is_valid_escape(current())) valid = false;
                advance();
            } else {
                advance();
            }
            ++chars;
        }

        if (eof() || current() == '\n') {
            return token(TokenKind::Error);
        }

        advance();

        if (chars == 0 || chars > 1 || !valid) {
            return token(TokenKind::Error);
        }

        return token(TokenKind::CharLiteral);
    }

    constexpr auto string_literal() noexcept -> Token {
        while (!eof() && current() != '"' && current() != '\n') {
            if (current() == '\\') {
                advance();
                if (eof() || current() == '\n') break;
                if (!is_valid_escape(current())) return token(TokenKind::Error);
                advance();
            } else {
                advance();
            }
        }

        if (!eof() && current() == '"') {
            advance();
        }

        return token(TokenKind::StringLiteral);
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

    constexpr auto token(TokenKind kind) const noexcept -> Token {
        return { kind, { start_pos, current_pos } };
    }
};

export constexpr auto tokenize(std::string_view source) noexcept -> std::vector<Token> {
    auto lexer = Lexer(source);
    auto tokens = std::vector<Token>();
    tokens.reserve(source.size() / 4);

    for (auto t = lexer.next(); t.kind != TokenKind::End; t = lexer.next()) {
        tokens.emplace_back(t);
    }

    return tokens;
}
