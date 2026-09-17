module carven:semantic.format.builtin;

import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.format;
import std;

// Text borrows completed input storage through the synchronous formatting operation.
// Monostate means unavailable or unsupported, independently of any execution value.
using BuiltinFormatValue = std::variant<
    std::monostate,
    IntegerConstant,
    F32Constant,
    F64Constant,
    BooleanConstant,
    CharacterConstant,
    std::string_view>;

enum class BuiltinFormatFailureKind { Unsupported, Limit };

struct BuiltinFormatFailure final {
    BuiltinFormatFailureKind kind;
    std::string_view message;
};

auto builtin_format_value(const ConstantValueReader& values, const ConstantFact& fact) noexcept
    -> BuiltinFormatValue;
auto resolve_builtin_format_specification(
    const FormatHole& hole,
    std::span<const BuiltinFormatValue> arguments,
    std::size_t budget,
    bool floating
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind>;
auto format_builtin_value(
    const BuiltinFormatValue& value,
    std::string_view specification,
    std::size_t budget
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind>;
auto format_builtin(
    const FormatSpec& specification,
    std::span<const BuiltinFormatValue> arguments,
    std::size_t budget
) noexcept -> std::expected<std::string, BuiltinFormatFailure>;
