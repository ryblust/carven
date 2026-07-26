module carven:frontend.lex.impl;

import :diagnostics.builder;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.lex;
import :frontend.lex.literal;
import :frontend.lex.token;
import :frontend.literal;
import :source.identifier;
import :source.text;
import :support.utf8;
import std;

namespace {

constexpr auto is_ascii_letter(char value) noexcept -> bool {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

constexpr auto is_decimal_digit(char value) noexcept -> bool {
    return value >= '0' && value <= '9';
}

constexpr auto is_identifier_start(char value) noexcept -> bool {
    return is_ascii_letter(value) || value == '_';
}

constexpr auto is_identifier_continue(char value) noexcept -> bool {
    return is_identifier_start(value) || is_decimal_digit(value);
}

constexpr auto token_kind(SourceKeyword keyword) noexcept -> TokenKind {
    switch (keyword) {
        using enum SourceKeyword;
        case As:       return TokenKind::As;
        case Break:    return TokenKind::Break;
        case Catch:    return TokenKind::Catch;
        case Const:    return TokenKind::Const;
        case Continue: return TokenKind::Continue;
        case Else:     return TokenKind::Else;
        case Enum:     return TokenKind::Enum;
        case Export:   return TokenKind::Export;
        case False:    return TokenKind::False;
        case Fn:       return TokenKind::Fn;
        case For:      return TokenKind::For;
        case If:       return TokenKind::If;
        case Import:   return TokenKind::Import;
        case In:       return TokenKind::In;
        case Is:       return TokenKind::Is;
        case Let:      return TokenKind::Let;
        case Match:    return TokenKind::Match;
        case Private:  return TokenKind::Private;
        case Return:   return TokenKind::Return;
        case Rethrow:  return TokenKind::Rethrow;
        case Struct:   return TokenKind::Struct;
        case Test:     return TokenKind::Test;
        case Throw:    return TokenKind::Throw;
        case True:     return TokenKind::True;
        case Try:      return TokenKind::Try;
        case Using:    return TokenKind::Using;
        case Var:      return TokenKind::Var;
        case While:    return TokenKind::While;
    }
    std::unreachable();
}

class Lexer final {
public:
    explicit Lexer(SourceView source_view) noexcept
        : source_id(source_view.source_id),
          source(source_view.text),
          result {
              .value = TokenBuffer(source_view.source_id),
              .diagnostics = {},
          } {
        result.value.reserve_tokens(source.size() / 4);
    }

    auto run() noexcept -> Diagnosed<TokenBuffer> {
        while (true) {
            skip_whitespace_and_comments();
            if (at_end()) {
                break;
            }

            scan_token();
        }

        return std::move(result);
    }

private:
    SourceID source_id;
    std::string_view source;
    std::uint32_t position = 0;
    std::uint32_t token_start = 0;
    Diagnosed<TokenBuffer> result;

    auto at_end(std::size_t lookahead = 0) const noexcept -> bool {
        return static_cast<std::size_t>(position) + lookahead >= source.size();
    }

    auto current(std::size_t lookahead = 0) const noexcept -> char {
        return at_end(lookahead) ? '\0' : source[position + lookahead];
    }

    auto advance() noexcept -> char {
        if (at_end()) {
            return '\0';
        }

        return source[position++];
    }

    auto match(char expected) noexcept -> bool {
        if (current() != expected) {
            return false;
        }

        ++position;
        return true;
    }

    auto span() const noexcept -> Span { return Span::from_bounds(token_start, position); }

    auto append_token(TokenKind kind) noexcept -> void { result.value.append_token(kind, span()); }

    auto append_literal_token(TokenLiteralValue value) noexcept -> void {
        result.value.append_literal_token(span(), std::move(value));
    }

    auto diagnose(std::string_view message, Span diagnostic_span) noexcept -> void {
        result.diagnostics.push_back(
            DiagnosticBuilder(DiagnosticCode::Lexical, std::string(message))
                .primary(locate(source_id, diagnostic_span))
                .build()
        );
    }

    auto diagnose_invalid(std::string_view message) noexcept -> void {
        append_token(TokenKind::Invalid);
        diagnose(message, span());
    }

    auto consume_utf8() noexcept -> bool {
        const auto start = position;
        const auto sequence = UTF8Decoder::decode(source, position);
        position += static_cast<std::uint32_t>(sequence.valid ? sequence.width : 1);
        if (!sequence.valid) {
            diagnose("invalid UTF-8 encoding", Span::from_bounds(start, position));
        }
        return sequence.valid;
    }

    auto skip_whitespace_and_comments() noexcept -> void {
        while (!at_end()) {
            if (current() == ' ' || current() == '\t' || current() == '\n' || current() == '\r') {
                ++position;
                continue;
            }

            if (current() == '/' && current(1) == '/') {
                position += 2;
                while (!at_end() && current() != '\n' && current() != '\r') {
                    if (static_cast<unsigned char>(current()) >= 0x80) {
                        consume_utf8();
                    } else {
                        ++position;
                    }
                }
                continue;
            }

            break;
        }
    }

    auto scan_token() noexcept -> void {
        token_start = position;
        const auto value = advance();

        if (is_identifier_start(value)) {
            scan_identifier();
            return;
        }

        if (is_decimal_digit(value)) {
            scan_number();
            return;
        }

        if (static_cast<unsigned char>(value) >= 0x80) {
            position = token_start;
            const auto valid = consume_utf8();
            append_token(TokenKind::Invalid);
            if (valid) {
                diagnose("non-ASCII characters are not valid identifiers", span());
            }
            return;
        }

        using enum TokenKind;
        switch (value) {
            case '(': append_token(LeftParen); return;
            case ')': append_token(RightParen); return;
            case '[': append_token(LeftBracket); return;
            case ']': append_token(RightBracket); return;
            case '{': append_token(LeftBrace); return;
            case '}': append_token(RightBrace); return;
            case ',': append_token(Comma); return;
            case '.': append_token(match('.') ? DotDot : Dot); return;
            case ';': append_token(Semicolon); return;
            case ':': append_token(match(':') ? ColonColon : Colon); return;
            case '+': append_token(match('=') ? PlusEqual : match('+') ? PlusPlus : Plus); return;
            case '-':
                append_token(
                    match('=')       ? MinusEqual
                        : match('-') ? MinusMinus
                        : match('>') ? Arrow
                                     : Minus
                );
                return;
            case '*': append_token(match('=') ? StarEqual : Star); return;
            case '/': append_token(match('=') ? SlashEqual : Slash); return;
            case '%': append_token(match('=') ? PercentEqual : Percent); return;
            case '!': append_token(match('=') ? BangEqual : Bang); return;
            case '=': append_token(match('=') ? EqualEqual : match('>') ? FatArrow : Equal); return;
            case '<':
                append_token(
                    match('<') ? (match('=') ? LeftShiftEqual : LeftShift)
                               : (match('=') ? LessEqual : Less)
                );
                return;
            case '>':
                append_token(
                    match('>') ? (match('=') ? RightShiftEqual : RightShift)
                               : (match('=') ? GreaterEqual : Greater)
                );
                return;
            case '&':
                append_token(
                    match('&')       ? AmpersandAmpersand
                        : match('=') ? AmpersandEqual
                                     : Ampersand
                );
                return;
            case '|':  append_token(match('|') ? PipePipe : match('=') ? PipeEqual : Pipe); return;
            case '^':  append_token(match('=') ? CaretEqual : Caret); return;
            case '~':  append_token(Tilde); return;
            case '?':  append_token(Question); return;
            case '"':  scan_string(); return;
            case '\'': scan_character(); return;
            case '#':  scan_cpp_region(); return;
            default:   diagnose_invalid("unknown source character"); return;
        }
    }

    auto scan_identifier() noexcept -> void {
        while (is_identifier_continue(current())) {
            ++position;
        }

        const auto spelling = source.substr(token_start, position - token_start);
        const auto classification = classify_identifier(spelling);
        const auto* keyword = std::get_if<KeywordIdentifier>(&classification);
        if (keyword) {
            append_token(token_kind(keyword->keyword));
        } else if (std::holds_alternative<OrdinaryIdentifier>(classification)) {
            append_token(TokenKind::Identifier);
        } else {
            diagnose_invalid("invalid identifier");
        }
    }

    auto scan_number() noexcept -> void {
        auto scanned = scan_numeric_literal(source.substr(token_start), token_start);
        const auto consumed = scanned.has_value() ? scanned->consumed : scanned.error().consumed;
        position = token_start + static_cast<std::uint32_t>(consumed);
        if (scanned.has_value()) {
            std::visit(
                [&](auto value) noexcept {
                    append_literal_token(TokenLiteralValue {std::move(value)});
                },
                std::move(scanned->value)
            );
        } else {
            diagnose_invalid(
                scanned.error().has_base_prefix ? "malformed has_base_prefix integer literal"
                                                : "malformed decimal literal"
            );
        }
    }

    auto scan_string() noexcept -> void {
        auto scanned = scan_string_literal(source.substr(token_start));
        const auto consumed = scanned.has_value() ? scanned->consumed : scanned.error().consumed;
        position = token_start + static_cast<std::uint32_t>(consumed);
        if (scanned.has_value()) {
            append_literal_token(std::move(scanned->value));
        } else {
            diagnose_invalid("malformed string literal");
        }
    }

    auto scan_character() noexcept -> void {
        auto scanned = scan_character_literal(source.substr(token_start));
        const auto consumed = scanned.has_value() ? scanned->consumed : scanned.error().consumed;
        position = token_start + static_cast<std::uint32_t>(consumed);
        if (scanned.has_value()) {
            append_literal_token(std::move(scanned->value));
        } else {
            diagnose_invalid("malformed character literal");
        }
    }

    auto scan_quoted_cpp(char quote) noexcept -> bool {
        ++position;
        while (!at_end()) {
            if (current() == '\\') {
                ++position;
                if (!at_end()) {
                    ++position;
                }
                continue;
            }
            if (current() == quote) {
                ++position;
                return true;
            }
            if (static_cast<unsigned char>(current()) >= 0x80) {
                consume_utf8();
            } else {
                ++position;
            }
        }
        return false;
    }

    auto raw_cpp_prefix_length() const noexcept -> std::size_t {
        static constexpr auto prefixes = std::to_array<std::string_view>({
            "u8R\"",
            "uR\"",
            "UR\"",
            "LR\"",
            "R\"",
        });
        for (const auto prefix : prefixes) {
            if (source.substr(position).starts_with(prefix)) {
                return prefix.size();
            }
        }
        return 0;
    }

    auto scan_raw_cpp(std::size_t prefix_length) noexcept -> bool {
        const auto start = position;
        position += static_cast<std::uint32_t>(prefix_length);
        const auto delimiter_start = position;
        while (!at_end()
               && current() != '('
               && current() != ' '
               && current() != ')'
               && current() != '\\'
               && current() != '\t'
               && current() != '\r'
               && current() != '\n') {
            ++position;
        }
        const auto delimiter_size = position - delimiter_start;
        if (delimiter_size > 16 || !match('(')) {
            // An invalid raw-string opener is ordinary payload, including its
            // introducer quote. Continue after that quote so it is not
            // reinterpreted as an ordinary string literal.
            position = start + static_cast<std::uint32_t>(prefix_length);
            return false;
        }

        const auto delimiter = source.substr(delimiter_start, position - delimiter_start - 1);
        while (!at_end()) {
            if (current() == ')'
                && source.substr(position + 1).starts_with(delimiter)
                && source.substr(position + 1 + delimiter.size()).starts_with('"')) {
                position += static_cast<std::uint32_t>(delimiter.size() + 2);
                return true;
            }
            if (static_cast<unsigned char>(current()) >= 0x80) {
                consume_utf8();
            } else {
                ++position;
            }
        }
        return true;
    }

    auto scan_cpp_region() noexcept -> void {
        if (!source.substr(token_start).starts_with("#[cpp]")) {
            diagnose_invalid("expected '#[cpp]' inline C++ introducer");
            return;
        }
        position = token_start + 6;
        while (current() == ' ' || current() == '\t' || current() == '\n' || current() == '\r') {
            ++position;
        }
        if (!match('{')) {
            diagnose_invalid("expected '{' after '#[cpp]'");
            return;
        }

        auto depth = 1uz;
        while (!at_end()) {
            if (current() == '/' && current(1) == '/') {
                position += 2;
                while (!at_end() && current() != '\n' && current() != '\r') {
                    if (static_cast<unsigned char>(current()) >= 0x80) {
                        consume_utf8();
                    } else {
                        ++position;
                    }
                }
                continue;
            }

            if (current() == '/' && current(1) == '*') {
                position += 2;
                auto closed = false;
                while (!at_end()) {
                    if (current() == '*' && current(1) == '/') {
                        position += 2;
                        closed = true;
                        break;
                    }
                    if (static_cast<unsigned char>(current()) >= 0x80) {
                        consume_utf8();
                    } else {
                        ++position;
                    }
                }
                if (!closed) {
                    break;
                }
                continue;
            }

            if (const auto prefix = raw_cpp_prefix_length(); prefix != 0 && scan_raw_cpp(prefix)) {
                continue;
            }

            if (current() == '"' || current() == '\'') {
                if (!scan_quoted_cpp(current())) {
                    break;
                }
                continue;
            }

            if (current() == '{') {
                ++depth;
                ++position;
                continue;
            }

            if (current() == '}') {
                --depth;
                ++position;
                if (depth == 0) {
                    append_token(TokenKind::CppRegion);
                    return;
                }
                continue;
            }

            if (static_cast<unsigned char>(current()) >= 0x80) {
                consume_utf8();
            } else {
                ++position;
            }
        }

        diagnose_invalid("unterminated inline C++ region");
    }
};

} // namespace

auto lex(SourceView source) noexcept -> Diagnosed<TokenBuffer> {
    return Lexer(source).run();
}
