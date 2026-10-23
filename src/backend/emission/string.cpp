module carven:backend.emission.string.impl;

import :backend.emission.string;
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

auto cpp_filename_token(std::string_view text) noexcept -> std::string {
    auto result = std::string("\"");
    result.reserve(text.size() + 2uz);
    static constexpr auto hexadecimal = std::string_view("0123456789abcdef");
    for (const auto character : text) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            case '\a': result += "\\a"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\v': result += "\\v"; break;
            default:   {
                const auto byte = static_cast<unsigned char>(character);
                if (byte < 0x20 || byte == 0x7f) {
                    result += "\\u00";
                    result.push_back(hexadecimal[byte >> 4]);
                    result.push_back(hexadecimal[byte & 0x0f]);
                } else {
                    result.push_back(character);
                }
                break;
            }
        }
    }
    result.push_back('"');
    return result;
}
