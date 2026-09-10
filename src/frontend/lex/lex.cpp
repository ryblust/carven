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
        case Nullptr:  return TokenKind::Nullptr;
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
    std::optional<TokenKind> previous_token_kind;
    Diagnosed<TokenBuffer> result;
    std::uint32_t interpolation_depth = 0;

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

    auto append_token(TokenKind kind) noexcept -> void {
        result.value.append_token(kind, span());
        previous_token_kind = kind;
    }

    auto append_literal_token(TokenLiteralValue value) noexcept -> void {
        result.value.append_literal_token(span(), std::move(value));
        previous_token_kind = result.value.tokens().back().kind;
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

    auto scan_interpolation(bool specification) noexcept -> void {
        if (++interpolation_depth > 512) {
            diagnose("interpolation nesting limit exceeded", span());
            position = static_cast<std::uint32_t>(source.size());
            --interpolation_depth;
            return;
        }
        auto bytes = std::string();
        token_start = position;
        const auto flush = [&]() noexcept {
            if (position != token_start) {
                append_literal_token(InterpolationTextValue {.bytes = std::move(bytes)});
                bytes.clear();
            }
        };
        while (!at_end()) {
            const auto value = current();
            if ((!specification && value == '"') || (specification && value == '}')) {
                flush();
                token_start = position++;
                append_token(
                    specification ? TokenKind::InterpolationClose : TokenKind::InterpolationEnd
                );
                --interpolation_depth;
                return;
            }
            if (value == '{' || value == '}') {
                if (!specification && current(1) == value) {
                    bytes.push_back(value);
                    position += 2;
                    continue;
                }
                flush();
                token_start = position++;
                if (value == '}') {
                    diagnose_invalid("unmatched '}' in interpolation text");
                } else {
                    append_token(TokenKind::InterpolationOpen);
                    scan_interpolation_expression();
                }
                token_start = position;
                continue;
            }
            const auto decoded = scan_literal_scalar(source.substr(position));
            if (!decoded) {
                const auto error_start =
                    position + static_cast<std::uint32_t>(decoded.error().error_offset);
                diagnose(
                    "invalid interpolation text",
                    Span::from_bounds(
                        error_start,
                        std::min(error_start + 1, static_cast<std::uint32_t>(source.size()))
                    )
                );
                position += static_cast<std::uint32_t>(std::max(decoded.error().consumed, 1uz));
                continue;
            }
            append_utf8(bytes, decoded->scalar);
            position += static_cast<std::uint32_t>(decoded->consumed);
        }
        flush();
        diagnose("unterminated interpolated string", span());
        --interpolation_depth;
    }

    auto scan_interpolation_expression() noexcept -> void {
        auto delimiters = std::vector<char>();
        while (true) {
            skip_whitespace_and_comments();
            if (at_end()) {
                diagnose("unterminated interpolation hole", span());
                return;
            }
            const auto value = current();
            if (delimiters.empty() && value == '}') {
                token_start = position++;
                append_token(TokenKind::InterpolationClose);
                return;
            }
            if (delimiters.empty() && value == ':' && current(1) != ':') {
                token_start = position++;
                append_token(TokenKind::InterpolationSpec);
                scan_interpolation(true);
                return;
            }
            if (value == '(' || value == '[' || value == '{') {
                delimiters.push_back(value);
            } else if (value == ')' || value == ']' || value == '}') {
                const auto expected = value == ')' ? '(' : value == ']' ? '[' : '{';
                if (delimiters.empty() || delimiters.back() != expected) {
                    token_start = position++;
                    diagnose_invalid("unmatched delimiter in interpolation hole");
                    return;
                }
                delimiters.pop_back();
            }
            scan_token();
        }
    }

    auto scan_token() noexcept -> void {
        token_start = position;
        const auto value = advance();

        if (value == 'f' && current() == '"') {
            ++position;
            append_token(TokenKind::InterpolationStart);
            scan_interpolation(false);
            return;
        }

        if (value == 'c' && current() == '"') {
            scan_string(true);
            return;
        }

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
                if (previous_token_kind == Import) {
                    scan_cpp_header_name('>', CppAngleHeaderName);
                    return;
                }
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
            case '|': append_token(match('|') ? PipePipe : match('=') ? PipeEqual : Pipe); return;
            case '^': append_token(match('=') ? CaretEqual : Caret); return;
            case '~': append_token(Tilde); return;
            case '?': append_token(Question); return;
            case '"':
                if (previous_token_kind == Import) {
                    scan_cpp_header_name('"', CppQuoteHeaderName);
                    return;
                }
                scan_string();
                return;
            case '\'': scan_character(); return;
            case '#':  scan_cpp_source_fragment(); return;
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
                scanned->value
            );
        } else {
            diagnose_invalid(
                scanned.error().has_base_prefix ? "malformed has_base_prefix integer literal"
                                                : "malformed decimal literal"
            );
        }
    }

    auto scan_string(bool c_string = false) noexcept -> void {
        const auto prefix = c_string ? 1u : 0u;
        auto scanned = scan_string_literal(source.substr(token_start + prefix), c_string);
        const auto consumed = scanned.has_value() ? scanned->consumed : scanned.error().consumed;
        position = token_start + prefix + static_cast<std::uint32_t>(consumed);
        if (scanned.has_value()) {
            if (c_string) {
                append_literal_token(
                    CStringLiteralValue {.bytes = std::move(scanned->value.bytes)}
                );
            } else {
                append_literal_token(std::move(scanned->value));
            }
        } else if (c_string) {
            append_token(TokenKind::Invalid);
            const auto start =
                token_start + prefix + static_cast<std::uint32_t>(scanned.error().error_offset);
            const auto end = std::min(
                position,
                start + static_cast<std::uint32_t>(scanned.error().error_length)
            );
            diagnose(
                "invalid C string literal: expected valid text without NUL",
                Span::from_bounds(start, end)
            );
        } else {
            diagnose_invalid("malformed string literal");
        }
    }

    auto scan_character() noexcept -> void {
        auto scanned = scan_character_literal(source.substr(token_start));
        const auto consumed = scanned.has_value() ? scanned->consumed : scanned.error().consumed;
        position = token_start + static_cast<std::uint32_t>(consumed);
        if (scanned.has_value()) {
            append_literal_token(scanned->value);
        } else {
            diagnose_invalid("malformed character literal");
        }
    }

    auto scan_cpp_header_name(char closing, TokenKind kind) noexcept -> void {
        const auto content_start = position;
        while (!at_end() && current() != closing && current() != '\n' && current() != '\r') {
            if (static_cast<unsigned char>(current()) >= 0x80) {
                consume_utf8();
            } else {
                ++position;
            }
        }
        if (position == content_start) {
            if (!at_end() && current() == closing) {
                ++position;
            }
            diagnose_invalid("a C++ header name must not be empty");
            return;
        }
        if (at_end() || current() != closing) {
            diagnose_invalid("unterminated C++ header name");
            return;
        }
        ++position;
        append_token(kind);
    }

    auto consume_line_ending() noexcept -> bool {
        if (match('\n')) {
            return true;
        }
        if (!match('\r')) {
            return false;
        }
        static_cast<void>(match('\n'));
        return true;
    }

    auto scan_cpp_source_fragment() noexcept -> void {
        if (!source.substr(token_start).starts_with("#[cpp]")) {
            diagnose_invalid("expected '#[cpp]' C++ source fragment introducer");
            return;
        }
        position = token_start + 6;
        if (current() != ' ' && current() != '\t') {
            diagnose_invalid("expected horizontal whitespace before the C++ source fragment fence");
            return;
        }
        while (current() == ' ' || current() == '\t') {
            ++position;
        }
        auto fence_size = 0uz;
        while (match('-')) {
            ++fence_size;
        }
        if (fence_size < 3) {
            diagnose_invalid("a C++ source fragment fence requires at least three '-' characters");
            return;
        }
        while (current() == ' ' || current() == '\t') {
            ++position;
        }
        if (!consume_line_ending()) {
            diagnose_invalid("expected a line ending after the C++ source fragment fence");
            return;
        }

        while (!at_end()) {
            const auto line_start = position;
            while (current() == ' ' || current() == '\t') {
                ++position;
            }
            auto closing_size = 0uz;
            while (match('-')) {
                ++closing_size;
            }
            while (current() == ' ' || current() == '\t') {
                ++position;
            }
            if (closing_size == fence_size
                && (at_end() || current() == '\n' || current() == '\r')) {
                append_token(TokenKind::CppSourceFragment);
                return;
            }
            position = line_start;
            while (!at_end() && current() != '\n' && current() != '\r') {
                if (static_cast<unsigned char>(current()) >= 0x80) {
                    consume_utf8();
                } else {
                    ++position;
                }
            }
            static_cast<void>(consume_line_ending());
        }

        diagnose_invalid("unterminated C++ source fragment; expected a matching fence");
    }
};

} // namespace

auto lex(SourceView source) noexcept -> Diagnosed<TokenBuffer> {
    return Lexer(source).run();
}
