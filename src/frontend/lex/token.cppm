module carven:frontend.lex.token;

import :frontend.literal;
import :source.text;
import std;

enum class TokenKind {
    Identifier,
    NumberLiteral,
    CharLiteral,
    StringLiteral,
    CStringLiteral,
    CppAngleHeaderName,
    CppQuoteHeaderName,
    CppSourceFragment,

    As,
    Break,
    Catch,
    Const,
    Continue,
    Else,
    Enum,
    Export,
    False,
    Fn,
    For,
    If,
    Import,
    In,
    Is,
    Let,
    Match,
    Private,
    Return,
    Struct,
    Test,
    Throw,
    True,
    Try,
    Rethrow,
    Using,
    Var,
    While,

    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Dot,
    DotDot,
    Colon,
    Semicolon,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Equal,
    Less,
    Greater,
    Ampersand,
    Pipe,
    Caret,
    Tilde,
    Question,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    BangEqual,
    EqualEqual,
    LessEqual,
    GreaterEqual,
    PlusPlus,
    MinusMinus,
    AmpersandAmpersand,
    PipePipe,
    LeftShift,
    RightShift,
    AmpersandEqual,
    PipeEqual,
    CaretEqual,
    LeftShiftEqual,
    RightShiftEqual,
    Arrow,
    FatArrow,
    ColonColon,

    Invalid,
};

struct Token final {
    TokenKind kind;
    Span span;
};

using TokenLiteralValue = std::variant<
    IntegerLiteralValue,
    FloatingLiteralValue,
    StringLiteralValue,
    CStringLiteralValue,
    CharacterLiteralValue>;

class TokenBuffer final {
public:
    explicit TokenBuffer(SourceID source_id) noexcept;
    TokenBuffer(const TokenBuffer&) = delete;
    TokenBuffer(TokenBuffer&&) = default;
    ~TokenBuffer() = default;

    auto operator=(const TokenBuffer&) -> TokenBuffer& = delete;
    auto operator=(TokenBuffer&&) -> TokenBuffer& = default;

    auto source_id() const noexcept -> SourceID;
    auto tokens() const noexcept -> std::span<const Token>;
    auto literal_value(std::size_t token_index) const noexcept -> const TokenLiteralValue&;

    auto reserve_tokens(std::size_t count) noexcept -> void;
    auto append_token(TokenKind kind, Span span) noexcept -> void;
    auto append_literal_token(Span span, TokenLiteralValue value) noexcept -> void;

private:
    SourceID source;
    std::vector<Token> token_storage;
    std::vector<std::uint32_t> literal_token_indices;
    std::vector<TokenLiteralValue> literal_values;
};

template<>
struct std::formatter<TokenKind> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }

    auto format(TokenKind kind, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", display_name(kind));
    }

private:
    static auto display_name(TokenKind kind) noexcept -> std::string_view;
};

template<>
struct std::formatter<Token> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }

    auto format(const Token& token, auto&& context) const noexcept {
        return std::format_to(
            context.out(),
            "{} [{}..{}]",
            token.kind,
            token.span.start(),
            token.span.end()
        );
    }
};
