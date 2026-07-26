module carven:frontend.lex.literal.impl;

import :frontend.lex.literal;
import :frontend.literal;
import :support.invariant;
import :support.utf8;
import std;

namespace {

struct NumericSuffixInfo final {
    std::string_view spelling;
    NumericSuffix suffix;
    bool floating;
};

constexpr auto numeric_suffix_catalog = std::to_array<NumericSuffixInfo>({
    {.spelling = "isize", .suffix = NumericSuffix::Isize, .floating = false},
    {.spelling = "usize", .suffix = NumericSuffix::Usize, .floating = false},
    {.spelling = "i16", .suffix = NumericSuffix::I16, .floating = false},
    {.spelling = "i32", .suffix = NumericSuffix::I32, .floating = false},
    {.spelling = "i64", .suffix = NumericSuffix::I64, .floating = false},
    {.spelling = "u16", .suffix = NumericSuffix::U16, .floating = false},
    {.spelling = "u32", .suffix = NumericSuffix::U32, .floating = false},
    {.spelling = "u64", .suffix = NumericSuffix::U64, .floating = false},
    {.spelling = "f32", .suffix = NumericSuffix::F32, .floating = true},
    {.spelling = "f64", .suffix = NumericSuffix::F64, .floating = true},
    {.spelling = "i8", .suffix = NumericSuffix::I8, .floating = false},
    {.spelling = "u8", .suffix = NumericSuffix::U8, .floating = false},
});

constexpr auto is_ascii_letter(char value) noexcept -> bool {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

constexpr auto is_decimal_digit(char value) noexcept -> bool {
    return value >= '0' && value <= '9';
}

constexpr auto is_hexadecimal_digit(char value) noexcept -> bool {
    return is_decimal_digit(value)
        || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

constexpr auto is_identifier_continue(char value) noexcept -> bool {
    return is_ascii_letter(value) || is_decimal_digit(value) || value == '_';
}

auto consume_suffix(std::string_view text, std::size_t& position, bool floating) noexcept
    -> NumericSuffix {
    for (const auto& info : numeric_suffix_catalog) {
        if (info.floating == floating && text.substr(position).starts_with(info.spelling)) {
            position += info.spelling.size();
            return info.suffix;
        }
    }
    return NumericSuffix::None;
}

auto conversion_status(std::errc error, bool consumed_all) noexcept -> NumericConversion {
    if (error == std::errc::result_out_of_range) {
        return NumericConversion::OutOfRange;
    }
    if (error != std::errc {} || !consumed_all) {
        invariant_violation("validated numeric literal failed conversion");
    }
    return NumericConversion::Exact;
}

constexpr auto integer_radix(IntegerBase base) noexcept -> int {
    switch (base) {
        case IntegerBase::Decimal:     return 10;
        case IntegerBase::Hexadecimal: return 16;
        case IntegerBase::Binary:      return 2;
        case IntegerBase::Octal:       return 8;
    }
    std::unreachable();
}

auto integer_value(
    std::string_view spelling,
    std::uint32_t source_offset,
    IntegerBase base,
    NumericSuffix suffix
) noexcept -> IntegerLiteralValue {
    const auto prefix_size = base == IntegerBase::Decimal ? 0uz : 2uz;
    auto magnitude = std::uint64_t {};
    const auto* first = spelling.data() + prefix_size;
    const auto* last = spelling.data() + spelling.size();
    const auto parsed = std::from_chars(first, last, magnitude, integer_radix(base));
    return {
        .value_span = Span::from_bounds(
            source_offset,
            source_offset + static_cast<std::uint32_t>(spelling.size())
        ),
        .magnitude = magnitude,
        .base = base,
        .suffix = suffix,
        .conversion = conversion_status(parsed.ec, parsed.ptr == last),
    };
}

auto floating_value(
    std::string_view spelling,
    std::uint32_t source_offset,
    NumericSuffix suffix
) noexcept -> FloatingLiteralValue {
    auto value = double {};
    const auto* first = spelling.data();
    const auto* last = spelling.data() + spelling.size();
    const auto parsed = std::from_chars(first, last, value, std::chars_format::general);
    return {
        .value_span = Span::from_bounds(
            source_offset,
            source_offset + static_cast<std::uint32_t>(spelling.size())
        ),
        .value = value,
        .suffix = suffix,
        .conversion = conversion_status(parsed.ec, parsed.ptr == last),
    };
}

constexpr auto hex_nibble(char digit) noexcept -> std::optional<std::uint32_t> {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return std::nullopt;
}

auto quoted_extent(std::string_view text, char quote) noexcept -> std::size_t {
    if (text.empty() || text.front() != quote) {
        return 0;
    }
    auto offset = 1uz;
    while (offset < text.size() && text[offset] != '\n' && text[offset] != '\r') {
        if (text[offset] == quote) {
            return offset + 1;
        }
        if (text[offset] == '\\' && offset + 1 < text.size()) {
            offset += 2;
        } else {
            ++offset;
        }
    }
    return offset;
}

struct QuotedWalk final {
    std::string bytes;
    std::size_t consumed;
    std::size_t scalar_count;
    char32_t scalar;
    std::size_t error_offset;
    bool valid;
};

enum class QuotedLiteralKind {
    String,
    Character,
};

auto walk_quoted_literal(std::string_view spelling, QuotedLiteralKind kind) noexcept -> QuotedWalk {
    auto result = QuotedWalk {
        .bytes = {},
        .consumed = 0uz,
        .scalar_count = 0uz,
        .scalar = 0,
        .error_offset = 0uz,
        .valid = false,
    };
    result.bytes.reserve(spelling.size());
    const auto quote = kind == QuotedLiteralKind::String ? '"' : '\'';
    if (spelling.empty() || spelling.front() != quote) {
        return result;
    }

    const auto fail = [&](std::size_t offset) noexcept {
        result.error_offset = offset;
        result.bytes.clear();
        return result;
    };
    for (auto offset = 1uz; offset < spelling.size();) {
        if (spelling[offset] == quote) {
            result.consumed = offset + 1;
            if (kind == QuotedLiteralKind::Character && result.scalar_count != 1) {
                return fail(offset);
            }
            result.valid = true;
            return result;
        }
        if (spelling[offset] == '\n' || spelling[offset] == '\r') {
            return fail(offset);
        }

        auto scalar = char32_t();
        if (spelling[offset] == '\\') {
            const auto escape_offset = offset++;
            if (offset >= spelling.size()) {
                return fail(escape_offset);
            }
            const auto escape = spelling[offset++];
            switch (escape) {
                case '0':  scalar = 0; break;
                case 'n':  scalar = '\n'; break;
                case 'r':  scalar = '\r'; break;
                case 't':  scalar = '\t'; break;
                case '\\': scalar = '\\'; break;
                case '\'': scalar = '\''; break;
                case '"':  scalar = '"'; break;
                case 'u':  {
                    if (offset >= spelling.size() || spelling[offset] != '{') {
                        return fail(escape_offset);
                    }
                    ++offset;
                    const auto digits_start = offset;
                    auto value = std::uint32_t {};
                    while (offset < spelling.size() && spelling[offset] != '}') {
                        const auto nibble = hex_nibble(spelling[offset]);
                        if (!nibble.has_value() || offset - digits_start >= 6) {
                            return fail(offset);
                        }
                        value = (value << 4) | *nibble;
                        ++offset;
                    }
                    if (offset == digits_start || offset >= spelling.size()) {
                        return fail(escape_offset);
                    }
                    ++offset;
                    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
                        return fail(digits_start);
                    }
                    scalar = static_cast<char32_t>(value);
                    break;
                }
                default: return fail(escape_offset);
            }
            if (escape == '0'
                && offset < spelling.size()
                && spelling[offset] >= '0'
                && spelling[offset] <= '7') {
                return fail(offset);
            }
        } else {
            const auto sequence = UTF8Decoder::decode(spelling, offset);
            if (!sequence.valid) {
                return fail(offset);
            }
            scalar = sequence.scalar;
            offset += sequence.width;
        }
        append_utf8(result.bytes, scalar);
        ++result.scalar_count;
        if (result.scalar_count == 1) {
            result.scalar = scalar;
        }
    }
    return fail(spelling.size());
}

} // namespace

auto scan_numeric_literal(std::string_view text, std::uint32_t source_offset) noexcept
    -> std::expected<NumericLiteralScan, NumericLiteralScanError> {
    if (text.empty() || !is_decimal_digit(text.front())) {
        return std::unexpected(
            NumericLiteralScanError {
                .consumed = 0uz,
                .error_offset = 0uz,
                .has_base_prefix = false,
            }
        );
    }

    auto position = 1uz;
    if (text.front() == '0'
        && position < text.size()
        && (text[position] == 'x'
            || text[position] == 'X'
            || text[position] == 'b'
            || text[position] == 'B'
            || text[position] == 'o'
            || text[position] == 'O')) {
        const auto prefix = text[position++];
        const auto base = prefix == 'x' || prefix == 'X' ? IntegerBase::Hexadecimal
            : prefix == 'b' || prefix == 'B'             ? IntegerBase::Binary
                                                         : IntegerBase::Octal;
        const auto digit_start = position;
        const auto valid_digit = [&](char value) noexcept -> bool {
            if (base == IntegerBase::Hexadecimal) {
                return is_hexadecimal_digit(value);
            }
            if (base == IntegerBase::Binary) {
                return value == '0' || value == '1';
            }
            return value >= '0' && value <= '7';
        };
        while (position < text.size() && valid_digit(text[position])) {
            ++position;
        }
        const auto value_size = position;
        const auto suffix = consume_suffix(text, position, false);
        auto valid = position != digit_start;
        auto error_offset = 0uz;
        if (position < text.size() && is_identifier_continue(text[position])) {
            if (valid) {
                error_offset = position;
            }
            valid = false;
            while (position < text.size() && is_identifier_continue(text[position])) {
                ++position;
            }
        }
        if (!valid && error_offset == 0) {
            error_offset = digit_start;
        }
        if (!valid) {
            return std::unexpected(
                NumericLiteralScanError {
                    .consumed = position,
                    .error_offset = error_offset,
                    .has_base_prefix = true,
                }
            );
        }
        return NumericLiteralScan {
            .consumed = position,
            .value = integer_value(text.substr(0, value_size), source_offset, base, suffix),
        };
    }

    auto error_offset = 0uz;
    while (position < text.size() && is_decimal_digit(text[position])) {
        ++position;
    }
    auto floating = false;
    auto malformed = false;
    if (position + 1 < text.size()
        && text[position] == '.'
        && is_decimal_digit(text[position + 1])) {
        floating = true;
        position += 2;
        while (position < text.size() && is_decimal_digit(text[position])) {
            ++position;
        }
    }
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
        floating = true;
        ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
            ++position;
        }
        if (position >= text.size() || !is_decimal_digit(text[position])) {
            malformed = true;
            error_offset = position;
        }
        while (position < text.size() && is_decimal_digit(text[position])) {
            ++position;
        }
    }

    const auto value_size = position;
    auto suffix = NumericSuffix::None;
    if (floating) {
        suffix = consume_suffix(text, position, true);
    } else {
        suffix = consume_suffix(text, position, true);
        if (suffix != NumericSuffix::None) {
            floating = true;
        } else {
            suffix = consume_suffix(text, position, false);
        }
    }
    if (position < text.size() && is_identifier_continue(text[position])) {
        if (!malformed) {
            error_offset = position;
        }
        malformed = true;
        while (position < text.size() && is_identifier_continue(text[position])) {
            ++position;
        }
    }
    if (malformed) {
        return std::unexpected(
            NumericLiteralScanError {
                .consumed = position,
                .error_offset = error_offset,
                .has_base_prefix = false,
            }
        );
    }
    return NumericLiteralScan {
        .consumed = position,
        .value = floating ? NumericLiteralValue {floating_value(
                                text.substr(0, value_size),
                                source_offset,
                                suffix
                            )}
                          : NumericLiteralValue {
                                integer_value(
                                    text.substr(0, value_size),
                                    source_offset,
                                    IntegerBase::Decimal,
                                    suffix
                                ),
                            },
    };
}

auto scan_string_literal(std::string_view text) noexcept
    -> std::expected<StringLiteralScan, QuotedLiteralScanError> {
    const auto extent = quoted_extent(text, '"');
    auto decoded = walk_quoted_literal(text.substr(0, extent), QuotedLiteralKind::String);
    const auto valid = decoded.valid && decoded.consumed == extent;
    if (!valid) {
        return std::unexpected(
            QuotedLiteralScanError {
                .consumed = extent,
                .error_offset = decoded.error_offset,
            }
        );
    }
    return StringLiteralScan {
        .consumed = extent,
        .value = StringLiteralValue {.bytes = std::move(decoded.bytes)},
    };
}

auto scan_character_literal(std::string_view text) noexcept
    -> std::expected<CharacterLiteralScan, QuotedLiteralScanError> {
    const auto extent = quoted_extent(text, '\'');
    const auto decoded = walk_quoted_literal(text.substr(0, extent), QuotedLiteralKind::Character);
    const auto valid = decoded.valid && decoded.consumed == extent;
    if (!valid) {
        return std::unexpected(
            QuotedLiteralScanError {
                .consumed = extent,
                .error_offset = decoded.error_offset,
            }
        );
    }
    return CharacterLiteralScan {
        .consumed = extent,
        .value = CharacterLiteralValue {.scalar = decoded.scalar},
    };
}
