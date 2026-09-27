module carven:frontend.lex.layout;

import std;

struct StringLayoutRemoval final {
    std::size_t offset;
    std::size_t length;
};

// Tracks physical text separately from decoded bytes. Holes have no byte width,
// but count as content; escaped whitespace never becomes layout whitespace.
class StringBlockLayout final {
public:
    StringBlockLayout() noexcept;
    auto append(std::string_view spelling, std::size_t byte_count) noexcept -> void;
    auto newline() noexcept -> void;
    auto hole() noexcept -> void;
    auto closing_line_is_blank() const noexcept -> bool;
    // Offsets address concatenated decoded text, excluding hole contents.
    auto finish() const noexcept -> std::vector<StringLayoutRemoval>;

private:
    struct Line final {
        std::size_t offset;
        std::string indent;
        bool content;
    };

    std::vector<Line> lines;
    std::size_t size = 0;
};

auto apply_string_layout(
    std::string_view bytes,
    std::size_t offset,
    std::span<const StringLayoutRemoval> removals
) noexcept -> std::string;
