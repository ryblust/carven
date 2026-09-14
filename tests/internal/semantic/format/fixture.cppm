module carven:test.internal.semantic.format.fixture;

import :semantic.semir.format;
import std;

auto format_text(std::string bytes) noexcept -> FormatPart {
    return {FormatText {.bytes = std::move(bytes)}};
}

auto format_field(std::size_t index) noexcept -> FormatPart {
    return {FormatHole {.operand_index = index, .has_specification = false, .specification = {}}};
}

auto format_field(std::size_t index, std::vector<FormatPart> specification) noexcept -> FormatPart {
    return {FormatHole {
        .operand_index = index,
        .has_specification = true,
        .specification = std::move(specification)
    }};
}
