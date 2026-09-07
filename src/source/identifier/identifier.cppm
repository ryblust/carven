module carven:source.identifier;

import std;

enum class SourceKeyword {
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
};

struct InvalidIdentifier final {};

struct OrdinaryIdentifier final {};

struct KeywordIdentifier final {
    SourceKeyword keyword;
};

using IdentifierClassification =
    std::variant<InvalidIdentifier, OrdinaryIdentifier, KeywordIdentifier>;

auto is_identifier_spelling(std::string_view spelling) noexcept -> bool;

auto classify_identifier(std::string_view spelling) noexcept -> IdentifierClassification;
