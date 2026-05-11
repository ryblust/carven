export module zero.frontend.lexer.token;

import zero.common.source;
import std;

export enum class TokenKind : std::uint32_t {
    Identifier,
    NumberLiteral,
    CharLiteral,
    StringLiteral,
    Keyword,

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

export struct Token final {
    TokenKind kind;
    Span span;
};

export auto display_token_type(TokenKind kind) noexcept -> const char*;

template<> struct std::formatter<TokenKind> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(TokenKind kind, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", display_token_type(kind));
    }
};

template<> struct std::formatter<Token> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(Token token, auto&& context) const noexcept {
        return std::format_to(context.out(),
            "{:<25}[{}..{}]",
            display_token_type(token.kind), token.span.start, token.span.end
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
