export module carven.frontend.token;

import carven.common.source;
import std;

export enum class TokenKind : std::uint32_t {
    Identifier,
    NumberLiteral,
    CharLiteral,
    StringLiteral,

    As,                     // as
    Const,                  // const
    Else,                   // else
    Enum,                   // enum
    Export,                 // export
    False,                  // false
    Fn,                     // fn
    For,                    // for
    If,                     // if
    Import,                 // import
    Let,                    // let
    Match,                  // match
    Return,                 // return
    Struct,                 // struct
    True,                   // true
    Using,                  // using
    Var,                    // var
    While,                  // while

    Comma,                  // ,
    Dot,                    // .
    Colon,                  // :
    SemiColon,              // ;
    LeftParen,              // (
    RightParen,             // )
    LeftBracket,            // [
    RightBracket,           // ]
    LeftBrace,              // {
    RightBrace,             // }

    Plus,                   // +
    Minus,                  // -
    Star,                   // *
    Slash,                  // /
    Percent,                // %
    Bang,                   // !
    Equal,                  // =
    Less,                   // <
    Greater,                // >
    PlusEqual,              // +=
    MinusEqual,             // -=
    StarEqual,              // *=
    SlashEqual,             // /=
    PercentEqual,           // %=
    BangEqual,              // !=
    EqualEqual,             // ==
    LessEqual,              // <=
    GreaterEqual,           // >=
    PlusPlus,               // ++
    MinusMinus,             // --

    Ampersand,              // &
    Pipe,                   // |
    Caret,                  // ^
    Tilde,                  // ~
    AmpersandAmpersand,     // &&
    PipePipe,               // ||
    LeftShift,              // <<
    RightShift,             // >>
    AmpersandEqual,         // &=
    PipeEqual,              // |=
    CaretEqual,             // ^=
    LeftShiftEqual,         // <<=
    RightShiftEqual,        // >>=

    Arrow,                  // ->
    FatArrow,               // =>
    ColonColon,             // ::

    End,
    Error,
    Unknown
};

constexpr auto to_string(TokenKind kind) noexcept -> const char* {
    switch (kind) {
        using enum TokenKind;

        case Identifier:            return "Identifier";

        case NumberLiteral:         return "NumberLiteral";
        case CharLiteral:           return "CharLiteral";
        case StringLiteral:         return "StringLiteral";

        case As:                    return "as";
        case Const:                 return "const";
        case Else:                  return "else";
        case Enum:                  return "enum";
        case Export:                return "export";
        case False:                 return "false";
        case Fn:                    return "fn";
        case For:                   return "for";
        case If:                    return "if";
        case Import:                return "import";
        case Let:                   return "let";
        case Match:                 return "match";
        case Return:                return "return";
        case Struct:                return "struct";
        case True:                  return "true";
        case Using:                 return "using";
        case Var:                   return "var";
        case While:                 return "while";

        case Comma:                 return "Comma";
        case Dot:                   return "Dot";
        case Colon:                 return "Colon";
        case SemiColon:             return "Semicolon";
        case LeftParen:             return "LeftParen";
        case RightParen:            return "RightParen";
        case LeftBracket:           return "LeftBracket";
        case RightBracket:          return "RightBracket";
        case LeftBrace:             return "LeftBrace";
        case RightBrace:            return "RightBrace";

        case Plus:                  return "Plus";
        case Minus:                 return "Minus";
        case Star:                  return "Star";
        case Slash:                 return "Slash";
        case Percent:               return "Percent";
        case Bang:                  return "Bang";
        case Equal:                 return "Equal";
        case Less:                  return "Less";
        case Greater:               return "Greater";

        case PlusEqual:             return "PlusEqual";
        case MinusEqual:            return "MinusEqual";
        case StarEqual:             return "StarEqual";
        case SlashEqual:            return "SlashEqual";
        case PercentEqual:          return "PercentEqual";
        case BangEqual:             return "BangEqual";
        case EqualEqual:            return "EqualEqual";
        case LessEqual:             return "LessEqual";
        case GreaterEqual:          return "GreaterEqual";
        case PlusPlus:              return "PlusPlus";
        case MinusMinus:            return "MinusMinus";

        case Arrow:                 return "Arrow";
        case FatArrow:              return "FatArrow";
        case ColonColon:            return "ColonColon";

        case AmpersandAmpersand:    return "AmpersandAmpersand";
        case PipePipe:              return "PipePipe";

        case Ampersand:             return "Ampersand";
        case Pipe:                  return "Pipe";
        case Caret:                 return "Caret";
        case Tilde:                 return "Tilde";
        case LeftShift:             return "LeftShift";
        case RightShift:            return "RightShift";
        case AmpersandEqual:        return "AmpersandEqual";
        case PipeEqual:             return "PipeEqual";
        case CaretEqual:            return "CaretEqual";
        case LeftShiftEqual:        return "LeftShiftEqual";
        case RightShiftEqual:       return "RightShiftEqual";

        case End:                   return "End";
        case Error:                 return "Error";
        default:                    return "Unknown";
    }
}

template<> struct std::formatter<TokenKind> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(TokenKind kind, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", to_string(kind));
    }
};

export struct Token final {
    TokenKind kind;
    Span span;
};

template<> struct std::formatter<Token> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(Token token, auto&& context) const noexcept {
        return std::format_to(
            context.out(),
            "{:<25}[{}..{}]",
            to_string(token.kind), token.span.start, token.span.end
        );
    }
};

template<> struct std::range_formatter<Token> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(const auto& tokens, auto&& context) const noexcept {
        if (tokens.empty()) return context.out();

        for (auto i = 0uz; i < tokens.size() - 1; ++i) {
            std::format_to(context.out(), "{}\n", tokens[i]);
        }

        return std::format_to(context.out(), "{}", tokens.back());
    }
};
