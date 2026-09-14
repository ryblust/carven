module carven:backend.preparation.format;

import :semantic.semir.constant_access;
import :semantic.semir.format;
import :semantic.semir.type;
import std;

constexpr auto maximum_prepared_format_bytes = 64uz * 1024uz;

struct IntegerFormatField final {
    int base;
    bool uppercase;
    bool zero_pad;
    std::uint32_t width;
    auto operator==(const IntegerFormatField&) const noexcept -> bool = default;
};

struct IntegerFormat final {
    // Unescaped literal segments before, between, and after the ordered fields.
    std::vector<std::string> text;
    std::vector<IntegerFormatField> fields;
    // Bounds from literal bytes, widths, and integer types. Equal bounds give
    // an exact size without evaluating the dynamic values.
    std::uint64_t minimum_size;
    std::uint64_t maximum_size;
    auto operator==(const IntegerFormat&) const noexcept -> bool = default;
};


enum class FormatResultEncoding { Unproven, ValidUTF8 };

struct PreparedFormatText final {
    std::string text;
};

struct PreparedIntegerFormat final {
    IntegerFormat format;
    std::vector<std::size_t> operand_indices;
};

struct PreparedDelegatedFormat final {
    std::string format_string;
    std::vector<std::size_t> operand_indices;
    FormatResultEncoding encoding;
};

using PreparedFormat =
    std::variant<PreparedFormatText, PreparedIntegerFormat, PreparedDelegatedFormat>;

auto prepared_format_operands(const PreparedFormat& preparation) noexcept
    -> std::span<const std::size_t>;

// Classifies ordered static integer fields and computes type-derived size bounds.
auto classify_integer_format(
    const FormatSpec& format,
    std::span<const std::optional<BuiltinType>> operands
) noexcept -> std::optional<IntegerFormat>;
// Proves UTF-8 on successful formatting for the supported builtin subset.
auto format_preserves_utf8(
    const FormatSpec& format,
    std::span<const std::optional<BuiltinType>> operands
) noexcept -> bool;

auto prepare_format(
    const ConstantValueReader& values,
    const FormatSpec& specification,
    std::span<const std::optional<BuiltinType>> types,
    std::span<const std::optional<ConstantID>> known
) noexcept -> PreparedFormat;
