module carven:support.dump.impl;

import :support.dump;
import :support.utf8;
import std;

auto escape_dump_value(std::string_view value) noexcept -> std::string {
    static constexpr auto digits = std::string_view("0123456789abcdef");
    auto result = std::string();
    result.reserve(value.size());

    const auto append_hex_escape = [&](unsigned char byte) noexcept {
        result += "\\x";
        result += digits[byte >> 4];
        result += digits[byte & 0x0f];
    };

    for (auto offset = 0uz; offset < value.size();) {
        const auto byte = static_cast<unsigned char>(value[offset]);
        if (byte >= 0x80) {
            const auto sequence = UTF8Decoder::decode(value, offset);
            if (!sequence.valid) {
                append_hex_escape(byte);
                ++offset;
            } else {
                result.append(value, offset, sequence.width);
                offset += sequence.width;
            }
            continue;
        }

        switch (byte) {
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (byte < 0x20 || byte == 0x7f) {
                    append_hex_escape(byte);
                } else {
                    result += static_cast<char>(byte);
                }
                break;
        }
        ++offset;
    }
    return result;
}

auto quote_dump_value(std::string_view value) noexcept -> std::string {
    return std::format("\"{}\"", escape_dump_value(value));
}
