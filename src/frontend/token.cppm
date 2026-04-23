export module zero.frontend.token;

import std;

export enum class TokenKind : std::uint64_t {
    Identifier,

    NumberLiteral,
    CharLiteral,
    StringLiteral,

    Keyword,

    Comma,              //  Lexeme: ','
    Dot,                //  Lexeme: '.'
    Colon,              //  Lexeme: ':'
    SemiColon,          //  Lexeme: ';'
    LeftParen,          //  Lexeme: '('
    RightParen,         //  Lexeme: ')'
    LeftBracket,        //  Lexeme: '['
    RightBracket,       //  Lexeme: ']'
    LeftBrace,          //  Lexeme: '{'
    RightBrace,         //  Lexeme: '}'

    Plus,               //  Lexeme: '+'
    Minus,              //  Lexeme: '-'
    Star,               //  Lexeme: '*'
    Slash,              //  Lexeme: '/'
    Percent,            //  Lexeme: '%'
    Bang,               //  Lexeme: '!'
    Equal,              //  Lexeme: '='
    Less,               //  Lexeme: '<'
    Greater,            //  Lexeme: '>'

    PlusEqual,          //  Lexeme: '+='
    MinusEqual,         //  Lexeme: '-='
    StarEqual,          //  Lexeme: '*='
    SlashEqual,         //  Lexeme: '/='
    PercentEqual,       //  Lexeme: '%='
    BangEqual,          //  Lexeme: '!='
    EqualEqual,         //  Lexeme: '=='
    LessEqual,          //  Lexeme: '<='
    GreaterEqual,       //  Lexeme: '>='
    PlusPlus,           //  Lexeme: '++'
    MinusMinus,         //  Lexeme: '--'

    Ampersand,          //  Lexeme: &
    Pipe,               //  Lexeme: |
    Caret,              //  Lexeme: ^
    Tilde,              //  Lexeme: ~

    AmpersandAmpersand, //  Lexeme: &&
    PipePipe,           //  Lexeme: ||

    LeftShift,          //  Lexeme: <<
    RightShift,         //  Lexeme: >>
    AmpersandEqual,     //  Lexeme: &=
    PipeEqual,          //  Lexeme: |=
    CaretEqual,         //  Lexeme: ^=
    LeftShiftEqual,     //  Lexeme: <<=
    RightShiftEqual,    //  Lexeme: >>=

    Arrow,              //  Lexeme: '->'
    FatArrow,           //  Lexeme: '=>'
    ColonColon,         //  Lexeme: '::'

    End,                //  Special: End of File
    Error,              //  Sepcial: Error Handling
    Unknown             //  Special: Unknown
};

export struct Token final {
    TokenKind kind;
    std::string_view lexeme;
    std::uint32_t line;
    std::uint32_t column;
};

static constexpr auto display_token_type(TokenKind kind) noexcept -> const char* {
    switch (kind) {
        using enum TokenKind;

        case Identifier:            return "Identifier";

        case NumberLiteral:         return "NumberLiteral";
        case CharLiteral:           return "CharLiteral";
        case StringLiteral:         return "StringLiteral";

        case Keyword:               return "Keyword";

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
        case Bang:                  return "Bang";
        case Equal:                 return "Equal";
        case Less:                  return "Less";
        case Greater:               return "Greater";

        case PlusEqual:             return "PlusEqual";
        case MinusEqual:            return "MinusEqual";
        case StarEqual:             return "StarEqual";
        case SlashEqual:            return "SlashEqual";
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

template<> struct std::formatter<Token> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    constexpr auto format(Token token, auto&& context) const noexcept {
        return std::format_to(context.out(),
            "{:<25}{:<25}Line: {:<15}Column: {:<15}",
            display_token_type(token.kind), token.lexeme, token.line, token.column
        );
    }
};

template<> struct std::range_formatter<Token> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    constexpr auto format(const auto& tokens, auto&& context) const noexcept {
        for (auto i = 0uz; i < tokens.size() - 1; i++) {
            std::format_to(context.out(), "{}\n", tokens[i]);
        }

        return std::format_to(context.out(), "{}", tokens.back());
    }
};