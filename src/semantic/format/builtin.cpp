module carven:semantic.format.builtin.impl;

import :semantic.format.builtin;
import :semantic.format;
import :semantic.semir.type;
import :support.utf8;
import std;

namespace {

template<typename Float>
auto format_floating(Float value, std::string_view specification, std::size_t budget) noexcept
    -> std::expected<std::string, BuiltinFormatFailureKind> {
    const auto parsed = parse_floating_format_specification(specification);
    if (!parsed) {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    for (const auto digits : {parsed->width, parsed->precision.value_or(std::string_view {})}) {
        if (digits.empty()) {
            continue;
        }
        auto number = std::uint64_t {};
        const auto converted =
            std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (converted.ec == std::errc::result_out_of_range
            || number > budget
            || number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return std::unexpected(BuiltinFormatFailureKind::Limit);
        }
        if (converted.ec != std::errc() || converted.ptr != digits.data() + digits.size()) {
            return std::unexpected(BuiltinFormatFailureKind::Unsupported);
        }
    }
    // Validate before entering the throwing native formatter.
    const auto format = std::format("{{:{}}}", specification);
    auto result = std::vformat(format, std::make_format_args(value));
    if (result.size() > budget) {
        return std::unexpected(BuiltinFormatFailureKind::Limit);
    }
    return result;
}

auto resolve_floating_specification(
    const FormatHole& hole,
    std::span<const BuiltinFormatValue> arguments,
    std::size_t budget
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind> {
    auto text = std::string();
    auto parameters = std::vector<std::uint64_t>();
    for (const auto& part : hole.specification) {
        if (const auto* literal = std::get_if<FormatText>(&part.value)) {
            if (literal->bytes.size() > budget - text.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Limit);
            }
            text += literal->bytes;
        } else {
            const auto& field = std::get<FormatHole>(part.value);
            if (field.has_specification
                || !field.specification.empty()
                || field.operand_index >= arguments.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            const auto* integer = std::get_if<IntegerConstant>(&arguments[field.operand_index]);
            if (integer == nullptr || integer->negative()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            if (integer->magnitude() > budget || budget - text.size() < 2uz) {
                return std::unexpected(BuiltinFormatFailureKind::Limit);
            }
            parameters.push_back(integer->magnitude());
            text += "{}";
        }
    }
    const auto parsed = parse_floating_format_specification(text);
    if (!parsed) {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    auto resolved = std::string();
    auto next = 0uz;
    for (auto index = 0uz; index < text.size();) {
        if (text[index] == '{') {
            if (next >= parameters.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            const auto value = parameters[next++];
            // Native dynamic width zero means no padding, not a literal zero-pad flag.
            if (value != 0 || parsed->width.data() != text.data() + index) {
                resolved += std::to_string(value);
            }
            index += 2uz;
        } else {
            resolved.push_back(text[index++]);
        }
        if (resolved.size() > budget) {
            return std::unexpected(BuiltinFormatFailureKind::Limit);
        }
    }
    if (next != parameters.size()) {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    return resolved;
}

auto format_integer(
    IntegerConstant value,
    std::string_view specification,
    std::size_t budget
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind> {
    const auto parsed = parse_integer_format_specification(specification);
    if (!parsed) {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    auto width = 0uz;
    if (!parsed->width.empty()) {
        const auto converted = std::from_chars(
            parsed->width.data(),
            parsed->width.data() + parsed->width.size(),
            width
        );
        if (converted.ec != std::errc()) {
            return std::unexpected(BuiltinFormatFailureKind::Unsupported);
        }
    }
    if (width > budget) {
        return std::unexpected(BuiltinFormatFailureKind::Limit);
    }
    auto buffer = std::array<char, 64>();
    const auto converted = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value.magnitude(),
        parsed->base
    );
    auto digits = std::string(buffer.data(), converted.ptr);
    if (parsed->uppercase) {
        for (auto& byte : digits) {
            if (byte >= 'a' && byte <= 'f') {
                byte = static_cast<char>(byte - 'a' + 'A');
            }
        }
    }
    const auto length = digits.size() + (value.negative() ? 1uz : 0uz);
    if (length > budget) {
        return std::unexpected(BuiltinFormatFailureKind::Limit);
    }
    const auto padding = width > length ? width - length : 0uz;
    auto result = std::string();
    if (!parsed->zero_pad) {
        result.append(padding, ' ');
    }
    if (value.negative()) {
        result.push_back('-');
    }
    if (parsed->zero_pad) {
        result.append(padding, '0');
    }
    result += digits;
    return result;
}

} // namespace

auto format_builtin_value(
    const BuiltinFormatValue& value,
    std::string_view specification,
    std::size_t budget
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind> {
    if (const auto* integer = std::get_if<IntegerConstant>(&value)) {
        return format_integer(*integer, specification, budget);
    }
    if (const auto* number = std::get_if<F32Constant>(&value)) {
        return format_floating(number->value, specification, budget);
    }
    if (const auto* number = std::get_if<F64Constant>(&value)) {
        return format_floating(number->value, specification, budget);
    }
    if (!specification.empty()) {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    auto result = std::string();
    if (const auto* text = std::get_if<std::string_view>(&value)) {
        if (text->size() > budget) {
            return std::unexpected(BuiltinFormatFailureKind::Limit);
        }
        result = *text;
    } else if (const auto* boolean = std::get_if<BooleanConstant>(&value)) {
        result = boolean->value ? "true" : "false";
    } else if (const auto* character = std::get_if<CharacterConstant>(&value)) {
        append_utf8(result, character->scalar);
    } else {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    if (result.size() > budget) {
        return std::unexpected(BuiltinFormatFailureKind::Limit);
    }
    return result;
}

auto resolve_builtin_format_specification(
    const FormatHole& hole,
    std::span<const BuiltinFormatValue> arguments,
    std::size_t budget,
    bool floating
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind> {
    if (floating) {
        return resolve_floating_specification(hole, arguments, budget);
    }
    auto result = std::string();
    auto dynamic_width = std::optional<std::uint64_t>();
    auto presentation = false;
    for (const auto& part : hole.specification) {
        if (const auto* text = std::get_if<FormatText>(&part.value)) {
            if (text->bytes.size() > budget - result.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Limit);
            }
            for (const auto byte : text->bytes) {
                if (dynamic_width) {
                    if (presentation
                        || std::string_view("bBdoxX").find(byte) == std::string_view::npos) {
                        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
                    }
                    presentation = true;
                }
                result.push_back(byte);
            }
        } else {
            const auto& width = std::get<FormatHole>(part.value);
            if (dynamic_width
                || (!result.empty() && result != "0")
                || width.has_specification
                || !width.specification.empty()
                || width.operand_index >= arguments.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            const auto* integer = std::get_if<IntegerConstant>(&arguments[width.operand_index]);
            if (integer == nullptr || integer->negative()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            if (integer->magnitude() > budget) {
                return std::unexpected(BuiltinFormatFailureKind::Limit);
            }
            dynamic_width = integer->magnitude();
        }
    }
    if (dynamic_width) {
        if (*dynamic_width != 0) {
            result.insert(result.starts_with('0') ? 1uz : 0uz, std::to_string(*dynamic_width));
        }
        // Keep a zero dynamic width an integer specification, even without padding.
        if (!presentation) {
            result.push_back('d');
        }
    }
    return result;
}

auto format_builtin(
    const FormatSpec& specification,
    std::span<const BuiltinFormatValue> arguments,
    std::size_t budget
) noexcept -> std::expected<std::string, BuiltinFormatFailure> {
    auto result = std::string();
    for (const auto& part : specification.parts) {
        auto fragment = std::string();
        if (const auto* text = std::get_if<FormatText>(&part.value)) {
            if (text->bytes.size() > budget - result.size()) {
                return std::unexpected(
                    BuiltinFormatFailure {
                        .kind = BuiltinFormatFailureKind::Limit,
                        .message = "constant format exceeds its text budget",
                    }
                );
            }
            fragment = text->bytes;
        } else {
            const auto& hole = std::get<FormatHole>(part.value);
            if (hole.operand_index >= arguments.size()) {
                return std::unexpected(
                    BuiltinFormatFailure {
                        .kind = BuiltinFormatFailureKind::Unsupported,
                        .message = "invalid constant format operand index"
                    }
                );
            }
            const auto evaluated = resolve_builtin_format_specification(
                hole,
                arguments,
                budget,
                std::holds_alternative<F32Constant>(arguments[hole.operand_index])
                    || std::holds_alternative<F64Constant>(arguments[hole.operand_index])
            );
            if (!evaluated) {
                return std::unexpected(
                    BuiltinFormatFailure {
                        .kind = evaluated.error(),
                        .message = evaluated.error() == BuiltinFormatFailureKind::Limit
                            ? "constant format exceeds its text budget"
                            : "constant format requires a valid specification and nonnegative integer width or precision",
                    }
                );
            }
            auto formatted =
                format_builtin_value(arguments[hole.operand_index], *evaluated, budget);
            if (!formatted) {
                return std::unexpected(
                    BuiltinFormatFailure {
                        .kind = formatted.error(),
                        .message = formatted.error() == BuiltinFormatFailureKind::Limit
                            ? "constant format exceeds its text budget"
                            : "constant format requires a supported builtin type and format specification",
                    }
                );
            }
            fragment = std::move(*formatted);
        }
        if (fragment.size() > budget - result.size()) {
            return std::unexpected(
                BuiltinFormatFailure {
                    .kind = BuiltinFormatFailureKind::Limit,
                    .message = "constant format exceeds its text budget"
                }
            );
        }
        result += fragment;
    }
    return result;
}

auto builtin_format_value(const ConstantValueReader& values, const ConstantFact& fact) noexcept
    -> BuiltinFormatValue {
    const auto type = values.type_copy(fact.type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
    if (builtin == nullptr) {
        return {};
    }
    if (const auto* integer = std::get_if<IntegerConstant>(&fact.value);
        integer != nullptr && builtin_is_integer(builtin->kind)) {
        return *integer;
    }
    if (const auto* number = std::get_if<F32Constant>(&fact.value)) {
        return *number;
    }
    if (const auto* number = std::get_if<F64Constant>(&fact.value)) {
        return *number;
    }
    if (const auto* text = std::get_if<StringConstant>(&fact.value)) {
        return values.spelling(text->value);
    }
    if (const auto* boolean = std::get_if<BooleanConstant>(&fact.value)) {
        return *boolean;
    }
    if (const auto* character = std::get_if<CharacterConstant>(&fact.value)) {
        return *character;
    }
    return {};
}
