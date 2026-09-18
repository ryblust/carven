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
    // An absent width consumes the operand immediately after the field value.
    std::optional<std::uint32_t> static_width;
    auto operator==(const IntegerFormatField&) const noexcept -> bool = default;
};

enum class FloatingFormatMode { Shortest, Fixed, Scientific, General };

struct FloatingFormatField final {
    FloatingFormatMode mode;
    std::uint32_t precision;
    auto operator==(const FloatingFormatField&) const noexcept -> bool = default;
};

using WriterFormatField = std::variant<IntegerFormatField, FloatingFormatField, BuiltinType>;

struct WriterFormat final {
    // Unescaped literal segments before, between, and after the ordered fields.
    std::vector<std::string> text;
    std::vector<WriterFormatField> fields;
    // Reservation bounds exclude dynamic-width fields and dynamic text bytes.
    // Text lengths are added by Writer; dynamic-width writes grow as needed.
    std::uint64_t minimum_size;
    std::uint64_t maximum_size;
    auto operator==(const WriterFormat&) const noexcept -> bool = default;
};


enum class FormatResultEncoding { Unproven, ValidUTF8 };

struct PreparedFormatText final {
    std::string text;
};

struct PreparedWriterFormat final {
    WriterFormat format;
    std::vector<std::size_t> operand_indices;
};

struct PreparedDelegatedFormat final {
    std::string format_string;
    std::vector<std::size_t> operand_indices;
    FormatResultEncoding encoding;
};

using PreparedFormat =
    std::variant<PreparedFormatText, PreparedWriterFormat, PreparedDelegatedFormat>;

auto prepared_format_operands(const PreparedFormat& preparation) noexcept
    -> std::span<const std::size_t>;

auto writer_field_operand_count(const WriterFormatField& field) noexcept -> std::size_t;

// Classifies fields; reservation bounds exclude dynamic text and dynamic-width fields.
auto classify_writer_format(
    const FormatSpec& format,
    std::span<const std::optional<BuiltinType>> operands
) noexcept -> std::optional<WriterFormat>;
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
