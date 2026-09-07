module carven:frontend.lex.token.impl;

import :frontend.lex.token;
import :support.invariant;
import std;

namespace {

auto requires_literal_value(TokenKind kind) noexcept -> bool {
    return kind == TokenKind::NumberLiteral
        || kind == TokenKind::CharLiteral
        || kind == TokenKind::StringLiteral
        || kind == TokenKind::CStringLiteral;
}

auto require_token_capacity(std::size_t token_count) noexcept -> void {
    if (token_count >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        resource_limit_exceeded("token buffer exhausted its 32-bit index space");
    }
}

auto literal_token_kind(const TokenLiteralValue& value) noexcept -> TokenKind {
    return std::visit(
        []<typename Value>(const Value&) static noexcept -> TokenKind {
            if constexpr (std::same_as<Value, IntegerLiteralValue>
                          || std::same_as<Value, FloatingLiteralValue>) {
                return TokenKind::NumberLiteral;
            } else if constexpr (std::same_as<Value, StringLiteralValue>) {
                return TokenKind::StringLiteral;
            } else if constexpr (std::same_as<Value, CStringLiteralValue>) {
                return TokenKind::CStringLiteral;
            } else {
                return TokenKind::CharLiteral;
            }
        },
        value
    );
}

} // namespace

TokenBuffer::TokenBuffer(SourceID source_id) noexcept
    : source(source_id) {}

auto TokenBuffer::source_id() const noexcept -> SourceID {
    return source;
}

auto TokenBuffer::tokens() const noexcept -> std::span<const Token> {
    return token_storage;
}

auto TokenBuffer::literal_value(std::size_t token_index) const noexcept
    -> const TokenLiteralValue& {
    if (token_index >= token_storage.size()) {
        invariant_violation("literal value lookup used an invalid token index");
    }
    const auto& token = token_storage[token_index];
    if (!requires_literal_value(token.kind)) {
        invariant_violation("token kind does not carry a literal value");
    }
    const auto stored_token_index = static_cast<std::uint32_t>(token_index);
    const auto found = std::ranges::lower_bound(literal_token_indices, stored_token_index);
    if (found == literal_token_indices.end() || *found != stored_token_index) {
        invariant_violation("literal token has no associated value");
    }
    const auto value_index = static_cast<std::size_t>(found - literal_token_indices.begin());
    if (value_index >= literal_values.size()) {
        invariant_violation("literal value associations are inconsistent");
    }
    const auto& value = literal_values[value_index];
    if (token.kind != literal_token_kind(value)) {
        invariant_violation("literal token kind does not match its value");
    }
    return value;
}

auto TokenBuffer::reserve_tokens(std::size_t count) noexcept -> void {
    token_storage.reserve(count);
}

auto TokenBuffer::append_token(TokenKind kind, Span span) noexcept -> void {
    if (requires_literal_value(kind)) {
        invariant_violation("literal token was appended without a value");
    }
    require_token_capacity(token_storage.size());
    token_storage.push_back({
        .kind = kind,
        .span = span,
    });
}

auto TokenBuffer::append_literal_token(Span span, TokenLiteralValue value) noexcept -> void {
    require_token_capacity(token_storage.size());
    const auto token_index = static_cast<std::uint32_t>(token_storage.size());
    token_storage.push_back({
        .kind = literal_token_kind(value),
        .span = span,
    });
    literal_token_indices.push_back(token_index);
    literal_values.push_back(std::move(value));
}

auto std::formatter<TokenKind>::display_name(TokenKind kind) noexcept -> std::string_view {
    switch (kind) {
        using enum TokenKind;
        case Identifier:         return "Identifier";
        case NumberLiteral:      return "NumberLiteral";
        case CharLiteral:        return "CharLiteral";
        case CStringLiteral:     return "CStringLiteral";
        case StringLiteral:      return "StringLiteral";
        case CppAngleHeaderName: return "CppAngleHeaderName";
        case CppQuoteHeaderName: return "CppQuoteHeaderName";
        case CppSourceFragment:  return "CppSourceFragment";
        case As:                 return "As";
        case Break:              return "Break";
        case Catch:              return "Catch";
        case Const:              return "Const";
        case Continue:           return "Continue";
        case Else:               return "Else";
        case Enum:               return "Enum";
        case Export:             return "Export";
        case False:              return "False";
        case Fn:                 return "Fn";
        case For:                return "For";
        case If:                 return "If";
        case Import:             return "Import";
        case In:                 return "In";
        case Is:                 return "Is";
        case Let:                return "Let";
        case Match:              return "Match";
        case Private:            return "Private";
        case Return:             return "Return";
        case Rethrow:            return "Rethrow";
        case Struct:             return "Struct";
        case Test:               return "Test";
        case Throw:              return "Throw";
        case True:               return "True";
        case Try:                return "Try";
        case Using:              return "Using";
        case Var:                return "Var";
        case While:              return "While";
        case LeftParen:          return "LeftParen";
        case RightParen:         return "RightParen";
        case LeftBracket:        return "LeftBracket";
        case RightBracket:       return "RightBracket";
        case LeftBrace:          return "LeftBrace";
        case RightBrace:         return "RightBrace";
        case Comma:              return "Comma";
        case Dot:                return "Dot";
        case DotDot:             return "DotDot";
        case Colon:              return "Colon";
        case Semicolon:          return "Semicolon";
        case Plus:               return "Plus";
        case Minus:              return "Minus";
        case Star:               return "Star";
        case Slash:              return "Slash";
        case Percent:            return "Percent";
        case Bang:               return "Bang";
        case Equal:              return "Equal";
        case Less:               return "Less";
        case Greater:            return "Greater";
        case Ampersand:          return "Ampersand";
        case Pipe:               return "Pipe";
        case Caret:              return "Caret";
        case Tilde:              return "Tilde";
        case Question:           return "Question";
        case PlusEqual:          return "PlusEqual";
        case MinusEqual:         return "MinusEqual";
        case StarEqual:          return "StarEqual";
        case SlashEqual:         return "SlashEqual";
        case PercentEqual:       return "PercentEqual";
        case BangEqual:          return "BangEqual";
        case EqualEqual:         return "EqualEqual";
        case LessEqual:          return "LessEqual";
        case GreaterEqual:       return "GreaterEqual";
        case PlusPlus:           return "PlusPlus";
        case MinusMinus:         return "MinusMinus";
        case AmpersandAmpersand: return "AmpersandAmpersand";
        case PipePipe:           return "PipePipe";
        case LeftShift:          return "LeftShift";
        case RightShift:         return "RightShift";
        case AmpersandEqual:     return "AmpersandEqual";
        case PipeEqual:          return "PipeEqual";
        case CaretEqual:         return "CaretEqual";
        case LeftShiftEqual:     return "LeftShiftEqual";
        case RightShiftEqual:    return "RightShiftEqual";
        case Arrow:              return "Arrow";
        case FatArrow:           return "FatArrow";
        case ColonColon:         return "ColonColon";
        case Invalid:            return "Invalid";
    }

    std::unreachable();
}
