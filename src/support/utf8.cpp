module carven:support.utf8.impl;

import :support.utf8;
import std;

auto UTF8Decoder::decode(std::string_view text, std::size_t offset) noexcept -> UTF8Sequence {
    if (offset >= text.size()) {
        return {.width = 0uz, .scalar = 0, .valid = false};
    }

    const auto byte_at = [&](std::size_t index) noexcept {
        return static_cast<unsigned char>(text[offset + index]);
    };
    const auto continuation = [&](std::size_t index) noexcept {
        return offset + index < text.size() && (byte_at(index) & 0xc0u) == 0x80u;
    };
    const auto lead = byte_at(0);

    if (lead < 0x80u) {
        return {.width = 1, .scalar = lead, .valid = true};
    }

    auto width = 0uz;
    if (lead >= 0xc2u && lead <= 0xdfu) {
        width = 2;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        width = 3;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        width = 4;
    } else {
        return {.width = 1, .scalar = 0xfffdu, .valid = false};
    }

    if (offset + width > text.size()) {
        return {.width = 1, .scalar = 0xfffdu, .valid = false};
    }
    for (auto index = 1uz; index < width; ++index) {
        if (!continuation(index)) {
            return {.width = 1, .scalar = 0xfffdu, .valid = false};
        }
    }

    const auto second = byte_at(1);
    if ((lead == 0xe0u && second < 0xa0u)
        || (lead == 0xedu && second > 0x9fu)
        || (lead == 0xf0u && second < 0x90u)
        || (lead == 0xf4u && second > 0x8fu)) {
        return {.width = 1, .scalar = 0xfffdu, .valid = false};
    }

    auto scalar = static_cast<char32_t>(lead & (width == 2 ? 0x1fu : width == 3 ? 0x0fu : 0x07u));
    for (auto index = 1uz; index < width; ++index) {
        scalar = static_cast<char32_t>((scalar << 6) | (byte_at(index) & 0x3fu));
    }
    return {.width = width, .scalar = scalar, .valid = true};
}

auto UTF8Decoder::is_valid(std::string_view text) noexcept -> bool {
    for (auto offset = 0uz; offset < text.size();) {
        const auto sequence = decode(text, offset);
        if (!sequence.valid) {
            return false;
        }
        offset += sequence.width;
    }
    return true;
}

auto append_utf8(std::string& output, char32_t scalar) noexcept -> void {
    if (scalar <= 0x7f) {
        output.push_back(static_cast<char>(scalar));
    } else if (scalar <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
        output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
    } else if (scalar <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
        output.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (scalar >> 18)));
        output.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
    }
}
