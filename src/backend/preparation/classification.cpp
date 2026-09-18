module carven:backend.preparation.classification.impl;

import :backend.preparation.format;
import :semantic.format;
import :support.utf8;
import std;

namespace {

auto maximum_integer_size(BuiltinType type, int base) noexcept -> std::uint32_t {
    const auto bits = *builtin_integer_width(type);
    const auto is_signed = builtin_is_signed_integer(type);
    auto magnitude = is_signed ? (1ull << (bits - 1u))
        : bits == 64u          ? std::numeric_limits<std::uint64_t>::max()
                               : (1ull << bits) - 1u;
    auto size = static_cast<std::uint32_t>(is_signed);
    do {
        ++size;
        magnitude /= static_cast<unsigned int>(base);
    } while (magnitude != 0);
    return size;
}

template<typename Text, typename Hole>
auto visit_format(
    const FormatSpec& format,
    std::size_t operand_count,
    Text text,
    Hole hole
) noexcept -> bool {
    auto next_operand = 0uz;
    for (const auto& part : format.parts) {
        if (const auto* literal = std::get_if<FormatText>(&part.value)) {
            if (!UTF8Decoder::is_valid(literal->bytes)) {
                return false;
            }
            for (const auto byte : literal->bytes) {
                text(byte);
            }
        } else {
            const auto& field = std::get<FormatHole>(part.value);
            if (field.operand_index != next_operand || next_operand >= operand_count) {
                return false;
            }
            ++next_operand;
            if (!hole(field, next_operand)) {
                return false;
            }
        }
    }
    return next_operand == operand_count;
}

auto static_specification(const FormatHole& field) noexcept -> std::optional<std::string> {
    auto specification = std::string();
    for (const auto& fragment : field.specification) {
        const auto* literal = std::get_if<FormatText>(&fragment.value);
        if (literal == nullptr) {
            return std::nullopt;
        }
        specification += literal->bytes;
    }
    return specification;
}

} // namespace

auto format_preserves_utf8(
    const FormatSpec& format,
    std::span<const std::optional<BuiltinType>> operands
) noexcept -> bool {
    return visit_format(
        format,
        operands.size(),
        [](char) static noexcept {},
        [&](const FormatHole& field, std::size_t&) noexcept {
            const auto index = field.operand_index;
            const auto specification = static_specification(field);
            if (!specification) {
                return false;
            }
            if (!operands[index]) {
                return false;
            }
            const auto type = *operands[index];
            if (builtin_is_integer(type)) {
                return parse_integer_format_specification(*specification).has_value();
            }
            if (type == BuiltinType::F32 || type == BuiltinType::F64) {
                return parse_floating_format_specification(*specification).has_value();
            }
            return specification->empty()
                && (type == BuiltinType::Bool
                    || type == BuiltinType::Char
                    || type == BuiltinType::Str
                    || type == BuiltinType::String);
        }
    );
}

auto classify_writer_format(
    const FormatSpec& format,
    std::span<const std::optional<BuiltinType>> operands
) noexcept -> std::optional<WriterFormat> {
    if (operands.empty()) {
        return std::nullopt;
    }
    auto result = WriterFormat {.text = {""}, .fields = {}, .minimum_size = 0u, .maximum_size = 0u};
    auto size_overflow = false;
    const auto add_size = [&](std::uint64_t minimum, std::uint64_t maximum) noexcept {
        if (maximum > std::numeric_limits<std::size_t>::max() - result.maximum_size) {
            size_overflow = true;
            return;
        }
        result.minimum_size += minimum;
        result.maximum_size += maximum;
    };
    const auto valid = visit_format(
        format,
        operands.size(),
        [&](char byte) noexcept {
            result.text.back().push_back(byte);
            add_size(1u, 1u);
        },
        [&](const FormatHole& field, std::size_t& next_operand) noexcept {
            const auto index = field.operand_index;
            auto specification = std::string();
            auto dynamic_width = false;
            for (const auto& fragment : field.specification) {
                if (const auto* text = std::get_if<FormatText>(&fragment.value)) {
                    specification += text->bytes;
                    continue;
                }
                const auto& width = std::get<FormatHole>(fragment.value);
                if (dynamic_width
                    || (specification != "" && specification != "0")
                    || width.has_specification
                    || !width.specification.empty()
                    || width.operand_index != next_operand
                    || next_operand >= operands.size()
                    || !operands[next_operand]
                    || !builtin_is_integer(*operands[next_operand])) {
                    return false;
                }
                const auto width_type = *operands[next_operand];
                const auto bits = *builtin_integer_width(width_type);
                if (bits > 32u || (bits == 32u && !builtin_is_signed_integer(width_type))) {
                    return false;
                }
                ++next_operand;
                dynamic_width = true;
                // A sentinel width lets the existing parser validate the surrounding policy.
                specification.push_back('1');
            }
            if (dynamic_width) {
                const auto parsed = parse_integer_format_specification(specification);
                if (!operands[index]
                    || !builtin_is_integer(*operands[index])
                    || !parsed
                    || parsed->width != "1") {
                    return false;
                }
                result.fields.emplace_back(
                    IntegerFormatField {
                        .base = parsed->base,
                        .uppercase = parsed->uppercase,
                        .zero_pad = parsed->zero_pad,
                        .static_width = std::nullopt,
                    }
                );
                result.text.emplace_back();
                return true;
            }
            if (!operands[index]) {
                return false;
            }
            const auto type = *operands[index];
            if (type == BuiltinType::F32 || type == BuiltinType::F64) {
                const auto parsed = parse_floating_format_specification(specification);
                if (!parsed || !parsed->unadorned) {
                    return false;
                }
                auto mode = FloatingFormatMode::Shortest;
                switch (parsed->presentation) {
                    case '\0':
                        mode = parsed->precision ? FloatingFormatMode::General
                                                 : FloatingFormatMode::Shortest;
                        break;
                    case 'f': mode = FloatingFormatMode::Fixed; break;
                    case 'e': mode = FloatingFormatMode::Scientific; break;
                    case 'g': mode = FloatingFormatMode::General; break;
                    default:  return false;
                }
                auto precision = std::uint32_t {6};
                if (parsed->precision) {
                    const auto digits = *parsed->precision;
                    const auto converted =
                        std::from_chars(digits.data(), digits.data() + digits.size(), precision);
                    // Bound the specialized stack buffer; larger precision uses std::format.
                    if (converted.ec != std::errc() || precision > 256u) {
                        return false;
                    }
                }
                result.fields.emplace_back(
                    FloatingFormatField {.mode = mode, .precision = precision}
                );
                result.text.emplace_back();
                const auto exponent = type == BuiltinType::F32
                    ? std::numeric_limits<float>::max_exponent10
                    : std::numeric_limits<double>::max_exponent10;
                add_size(1u, static_cast<std::uint64_t>(exponent) + precision + 32u);
                return true;
            }
            if (!builtin_is_integer(type)) {
                if (!specification.empty()) {
                    return false;
                }
                switch (type) {
                    case BuiltinType::Bool:   add_size(4u, 5u); break;
                    case BuiltinType::Char:   add_size(1u, 4u); break;
                    case BuiltinType::Str:
                    case BuiltinType::String: break;
                    default:                  return false;
                }
                result.fields.emplace_back(type);
                result.text.emplace_back();
                return true;
            }
            const auto parsed = parse_integer_format_specification(specification);
            if (!parsed) {
                return false;
            }
            auto width = std::uint32_t {0};
            if (!parsed->width.empty()) {
                const auto converted = std::from_chars(
                    parsed->width.data(),
                    parsed->width.data() + parsed->width.size(),
                    width
                );
                // Keep larger native width representations on the general path.
                if (converted.ec != std::errc()
                    || width > std::numeric_limits<std::int32_t>::max()) {
                    return false;
                }
            }
            result.fields.push_back(
                IntegerFormatField {
                    .base = parsed->base,
                    .uppercase = parsed->uppercase,
                    .zero_pad = parsed->zero_pad,
                    .static_width = width,
                }
            );
            result.text.emplace_back();
            add_size(
                std::max(width, 1u),
                std::max(width, maximum_integer_size(*operands[index], parsed->base))
            );
            return true;
        }
    );
    return valid && !size_overflow ? std::optional(std::move(result)) : std::nullopt;
}
