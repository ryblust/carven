module carven:source.identifier.impl;

import :source.identifier;
import std;

auto is_identifier_spelling(std::string_view spelling) noexcept -> bool {
    const auto is_ascii_letter = [](char value) static noexcept {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    };
    const auto is_identifier_start = [&](char value) noexcept {
        return is_ascii_letter(value) || value == '_';
    };
    const auto is_identifier_continue = [&](char value) noexcept {
        return is_identifier_start(value) || (value >= '0' && value <= '9');
    };

    if (spelling.empty() || !is_identifier_start(spelling.front())) {
        return false;
    }
    for (const auto value : spelling.substr(1)) {
        if (!is_identifier_continue(value)) {
            return false;
        }
    }

    return true;
}

auto classify_identifier(std::string_view spelling) noexcept -> IdentifierClassification {
    if (!is_identifier_spelling(spelling)) {
        return InvalidIdentifier {};
    }

    static constexpr auto keywords = std::to_array<std::pair<std::string_view, SourceKeyword>>({
        {"as", SourceKeyword::As},
        {"break", SourceKeyword::Break},
        {"catch", SourceKeyword::Catch},
        {"const", SourceKeyword::Const},
        {"continue", SourceKeyword::Continue},
        {"else", SourceKeyword::Else},
        {"enum", SourceKeyword::Enum},
        {"export", SourceKeyword::Export},
        {"false", SourceKeyword::False},
        {"fn", SourceKeyword::Fn},
        {"for", SourceKeyword::For},
        {"if", SourceKeyword::If},
        {"import", SourceKeyword::Import},
        {"in", SourceKeyword::In},
        {"is", SourceKeyword::Is},
        {"let", SourceKeyword::Let},
        {"match", SourceKeyword::Match},
        {"private", SourceKeyword::Private},
        {"return", SourceKeyword::Return},
        {"rethrow", SourceKeyword::Rethrow},
        {"struct", SourceKeyword::Struct},
        {"test", SourceKeyword::Test},
        {"throw", SourceKeyword::Throw},
        {"true", SourceKeyword::True},
        {"try", SourceKeyword::Try},
        {"using", SourceKeyword::Using},
        {"var", SourceKeyword::Var},
        {"while", SourceKeyword::While},
    });

    for (const auto [keyword_spelling, keyword] : keywords) {
        if (spelling == keyword_spelling) {
            return KeywordIdentifier {.keyword = keyword};
        }
    }

    return OrdinaryIdentifier {};
}
