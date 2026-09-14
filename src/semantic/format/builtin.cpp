module carven:semantic.format.builtin.impl;

import :semantic.format.builtin;
import :semantic.format;
import :semantic.semir.type;
import :support.utf8;
import std;

namespace {

auto format_integer(
    IntegerConstant value,
    std::string_view specification,
    std::size_t budget,
    std::optional<std::uint64_t> dynamic_width
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind> {
    const auto parsed = parse_integer_format_specification(specification);
    if (!parsed || (dynamic_width && !parsed->width.empty())) {
        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
    }
    auto width = 0uz;
    if (dynamic_width) {
        if (*dynamic_width > budget) {
            return std::unexpected(BuiltinFormatFailureKind::Limit);
        }
        width = static_cast<std::size_t>(*dynamic_width);
    } else if (!parsed->width.empty()) {
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
    std::size_t budget,
    std::optional<std::uint64_t> dynamic_width
) noexcept -> std::expected<std::string, BuiltinFormatFailureKind> {
    if (const auto* integer = std::get_if<IntegerConstant>(&value)) {
        return format_integer(*integer, specification, budget, dynamic_width);
    }
    if (!specification.empty() || dynamic_width) {
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
    std::size_t budget
) noexcept -> std::expected<ResolvedBuiltinFormatSpecification, BuiltinFormatFailureKind> {
    auto result = ResolvedBuiltinFormatSpecification {.text = {}, .width = std::nullopt};
    auto presentation = false;
    for (const auto& part : hole.specification) {
        if (const auto* text = std::get_if<FormatText>(&part.value)) {
            if (text->bytes.size() > budget - result.text.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Limit);
            }
            for (const auto byte : text->bytes) {
                if (result.width) {
                    if (presentation
                        || std::string_view("bBdoxX").find(byte) == std::string_view::npos) {
                        return std::unexpected(BuiltinFormatFailureKind::Unsupported);
                    }
                    presentation = true;
                }
                result.text.push_back(byte);
            }
        } else {
            const auto& width = std::get<FormatHole>(part.value);
            if (result.width
                || (!result.text.empty() && result.text != "0")
                || width.has_specification
                || !width.specification.empty()
                || width.operand_index >= arguments.size()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            const auto* integer = std::get_if<IntegerConstant>(&arguments[width.operand_index]);
            if (integer == nullptr || integer->negative()) {
                return std::unexpected(BuiltinFormatFailureKind::Unsupported);
            }
            result.width = integer->magnitude();
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
            const auto evaluated = resolve_builtin_format_specification(hole, arguments, budget);
            if (!evaluated) {
                return std::unexpected(
                    BuiltinFormatFailure {
                        .kind = evaluated.error(),
                        .message = evaluated.error() == BuiltinFormatFailureKind::Limit
                            ? "constant format exceeds its text budget"
                            : "dynamic constant format width requires a nonnegative integer argument",
                    }
                );
            }
            auto formatted = format_builtin_value(
                arguments[hole.operand_index],
                evaluated->text,
                budget,
                evaluated->width
            );
            if (!formatted) {
                return std::unexpected(
                    BuiltinFormatFailure {
                        .kind = formatted.error(),
                        .message = formatted.error() == BuiltinFormatFailureKind::Limit
                            ? "constant format exceeds its text budget"
                            : "const formatting supports default scalar/text formatting and integer bases with decimal width or zero padding",
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
