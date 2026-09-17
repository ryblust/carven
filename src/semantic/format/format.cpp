module carven:semantic.format.impl;

import :semantic.format;
import :semantic.semir.type;
import :support.utf8;
import std;

auto parse_floating_format_specification(std::string_view specification) noexcept
    -> std::optional<FloatingFormatSpecification> {
    auto result = FloatingFormatSpecification {
        .width = {},
        .precision = std::nullopt,
        .presentation = '\0',
        .unadorned = true
    };
    const auto alignment = [](char byte) static noexcept {
        return byte == '<' || byte == '>' || byte == '^';
    };
    if (!specification.empty()) {
        const auto fill = UTF8Decoder::decode(specification, 0uz);
        if (fill.valid
            && fill.width < specification.size()
            && alignment(specification[fill.width])) {
            if (fill.scalar == U'{' || fill.scalar == U'}') {
                return std::nullopt;
            }
            specification.remove_prefix(fill.width + 1uz);
            result.unadorned = false;
        } else if (alignment(specification.front())) {
            specification.remove_prefix(1uz);
            result.unadorned = false;
        }
    }
    for (const auto options :
         {std::string_view("+- "), std::string_view("#"), std::string_view("0")}) {
        if (!specification.empty()
            && options.find(specification.front()) != std::string_view::npos) {
            specification.remove_prefix(1uz);
            result.unadorned = false;
        }
    }
    const auto number = [&]() noexcept -> std::string_view {
        const auto original = specification;
        if (specification.starts_with("{}")) {
            specification.remove_prefix(2uz);
            return original.substr(0uz, 2uz);
        }
        while (!specification.empty()
               && specification.front() >= '0'
               && specification.front() <= '9') {
            specification.remove_prefix(1uz);
        }
        return original.substr(0uz, original.size() - specification.size());
    };
    result.width = number();
    if (!result.width.empty()) {
        if (result.width.front() == '0') {
            return std::nullopt;
        }
        result.unadorned = false;
    }
    if (specification.starts_with('.')) {
        specification.remove_prefix(1uz);
        result.precision = number();
        if (result.precision->empty()) {
            return std::nullopt;
        }
    }
    if (!specification.empty()
        && std::string_view("aAeEfFgG").find(specification.front()) != std::string_view::npos) {
        result.presentation = specification.front();
        specification.remove_prefix(1uz);
    }
    return specification.empty() ? std::optional(result) : std::nullopt;
}

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
