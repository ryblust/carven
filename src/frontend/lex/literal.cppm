module carven:frontend.lex.literal;

import :frontend.literal;
import :source.text;
import std;

struct NumericLiteralScan final {
    std::size_t consumed;
    NumericLiteralValue value;
};

struct NumericLiteralScanError final {
    std::size_t consumed;
    std::size_t error_offset;
    bool has_base_prefix;
};

auto scan_numeric_literal(std::string_view text, std::uint32_t source_offset) noexcept
    -> std::expected<NumericLiteralScan, NumericLiteralScanError>;

struct StringLiteralScan final {
    std::size_t consumed;
    StringLiteralValue value;
};

struct CharacterLiteralScan final {
    std::size_t consumed;
    CharacterLiteralValue value;
};

struct QuotedLiteralScanError final {
    std::size_t consumed;
    std::size_t error_offset;
    std::size_t error_length;
};

auto scan_string_literal(std::string_view text, bool reject_nul = false) noexcept
    -> std::expected<StringLiteralScan, QuotedLiteralScanError>;

auto scan_character_literal(std::string_view text) noexcept
    -> std::expected<CharacterLiteralScan, QuotedLiteralScanError>;
