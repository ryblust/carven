module carven:support.utf8;

import std;

struct UTF8Sequence final {
    std::size_t width;
    char32_t scalar;
    bool valid;
};

class UTF8Decoder final {
public:
    static auto decode(std::string_view text, std::size_t offset) noexcept -> UTF8Sequence;

    static auto is_valid(std::string_view text) noexcept -> bool;
};

auto append_utf8(std::string& output, char32_t scalar) noexcept -> void;
