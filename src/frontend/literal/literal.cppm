module carven:frontend.literal;

import :source.text;
import std;

enum class NumericSuffix {
    None,
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Isize,
    Usize,
    F32,
    F64,
};

enum class IntegerBase {
    Decimal,
    Hexadecimal,
    Binary,
    Octal,
};

enum class NumericConversion {
    Exact,
    OutOfRange,
};

struct IntegerLiteralValue final {
    Span value_span;
    std::uint64_t magnitude;
    IntegerBase base;
    NumericSuffix suffix;
    NumericConversion conversion;
};

struct FloatingLiteralValue final {
    Span value_span;
    double value;
    NumericSuffix suffix;
    NumericConversion conversion;
};

struct StringLiteralValue final {
    std::string bytes;
};

struct CStringLiteralValue final {
    std::string bytes;
};

struct CharacterLiteralValue final {
    char32_t scalar;
};

struct BooleanLiteralValue final {
    bool value;
};

using NumericLiteralValue = std::variant<IntegerLiteralValue, FloatingLiteralValue>;

auto numeric_suffix(const NumericLiteralValue& value) noexcept -> NumericSuffix;

template<>
struct std::formatter<NumericSuffix> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }

    auto format(NumericSuffix suffix, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", display_name(suffix));
    }

private:
    static auto display_name(NumericSuffix suffix) noexcept -> std::string_view;
};
