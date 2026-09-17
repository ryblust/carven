module carven:semantic.format;

import :semantic.semir.format;
import :semantic.semir.type;
import std;

struct IntegerFormatSpecification final {
    int base;
    bool uppercase;
    bool zero_pad;
    // Decimal width digits borrow the input specification. No execution budget
    // or target-library width limit is part of this syntax classification.
    std::string_view width;
};

// Width and precision borrow decimal digits or a normalized dynamic "{}" field.
struct FloatingFormatSpecification final {
    std::string_view width;
    std::optional<std::string_view> precision;
    char presentation;
    // No alignment, fill, sign, alternate form, zero padding, or width.
    bool unadorned;
};

auto parse_floating_format_specification(std::string_view specification) noexcept
    -> std::optional<FloatingFormatSpecification>;

auto parse_integer_format_specification(std::string_view specification) noexcept
    -> std::optional<IntegerFormatSpecification>;

// The budget bounds serialized bytes, including escaped braces and field indices.
auto serialize_format(const FormatSpec& specification, std::size_t budget) noexcept
    -> std::optional<std::string>;
auto serialize_format(const FormatSpec& specification) noexcept -> std::string;
auto format_operands(const FormatSpec& specification) noexcept -> std::vector<std::size_t>;
