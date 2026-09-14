module carven:semantic.format.impl;

import :semantic.format;
import :semantic.semir.type;
import :support.utf8;
import std;

auto parse_integer_format_specification(std::string_view specification) noexcept
    -> std::optional<IntegerFormatSpecification> {
    auto result = IntegerFormatSpecification {
        .base = 10,
        .uppercase = false,
        .zero_pad = false,
        .width = {},
    };
    if (!specification.empty()) {
        auto presentation = true;
        switch (specification.back()) {
            case 'b': result.base = 2; break;
            case 'B':
                result.base = 2;
                result.uppercase = true;
                break;
            case 'o': result.base = 8; break;
            case 'd': break;
            case 'x': result.base = 16; break;
            case 'X':
                result.base = 16;
                result.uppercase = true;
                break;
            default: presentation = false; break;
        }
        if (presentation) {
            specification.remove_suffix(1);
        }
    }
    result.zero_pad = specification.starts_with('0');
    if (result.zero_pad) {
        specification.remove_prefix(1);
    }
    if (!specification.empty()
        && (specification.front() < '1'
            || specification.front() > '9'
            || !std::ranges::all_of(specification, [](char byte) static noexcept {
                   return byte >= '0' && byte <= '9';
               }))) {
        return std::nullopt;
    }
    result.width = specification;
    return result;
}

auto serialize_format(const FormatSpec& specification, std::size_t budget) noexcept
    -> std::optional<std::string> {
    auto result = std::string();
    const auto write = [&](std::string_view bytes) noexcept -> bool {
        if (bytes.size() > budget - result.size()) {
            return false;
        }
        result += bytes;
        return true;
    };
    const auto append = [&](this const auto& self,
                            std::span<const FormatPart> parts,
                            bool nested) noexcept -> bool {
        for (const auto& part : parts) {
            if (const auto* text = std::get_if<FormatText>(&part.value)) {
                if (nested) {
                    if (!write(text->bytes)) {
                        return false;
                    }
                    continue;
                }
                for (const auto byte : text->bytes) {
                    if (!write(std::string_view(&byte, 1uz))
                        || ((byte == '{' || byte == '}') && !write(std::string_view(&byte, 1uz)))) {
                        return false;
                    }
                }
            } else {
                const auto& hole = std::get<FormatHole>(part.value);
                if (!write("{") || !write(std::to_string(hole.operand_index))) {
                    return false;
                }
                if (hole.has_specification && (!write(":") || !self(hole.specification, true))) {
                    return false;
                }
                if (!write("}")) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!append(specification.parts, false)) {
        return std::nullopt;
    }
    return result;
}

auto serialize_format(const FormatSpec& specification) noexcept -> std::string {
    return *serialize_format(specification, std::numeric_limits<std::size_t>::max());
}

auto format_operands(const FormatSpec& specification) noexcept -> std::vector<std::size_t> {
    auto result = std::vector<std::size_t>();
    const auto visit = [&](this const auto& self,
                           std::span<const FormatPart> parts) noexcept -> void {
        for (const auto& part : parts) {
            if (const auto* hole = std::get_if<FormatHole>(&part.value)) {
                result.push_back(hole->operand_index);
                self(hole->specification);
            }
        }
    };
    visit(specification.parts);
    return result;
}
