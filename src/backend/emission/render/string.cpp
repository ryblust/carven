module carven:backend.emission.render.string.impl;

import :backend.emission.render.string;
import std;

auto cpp_string_token(std::string_view bytes) noexcept -> std::string {
    auto result = std::string {"\""};
    static constexpr auto octal = std::string_view {"01234567"};
    for (const auto raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte >= 0x20 && byte <= 0x7e && byte != '\\' && byte != '"') {
            result.push_back(static_cast<char>(byte));
        } else if (byte == '\\') {
            result += "\\\\";
        } else if (byte == '"') {
            result += "\\\"";
        } else {
            result.push_back('\\');
            result.push_back(octal[(byte >> 6) & 0x07]);
            result.push_back(octal[(byte >> 3) & 0x07]);
            result.push_back(octal[byte & 0x07]);
        }
    }
    result.push_back('"');
    return result;
}
