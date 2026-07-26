module carven:support.path.impl;

import :support.path;
import std;

auto path_from_utf8(std::string_view value) noexcept -> std::filesystem::path {
    auto encoded = std::u8string();
    encoded.reserve(value.size());
    for (const auto byte : value) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path(encoded);
}

auto path_to_generic_utf8(const std::filesystem::path& path) noexcept -> std::string {
    const auto encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}
