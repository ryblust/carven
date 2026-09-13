module carven:source.location;

import std;

struct SourceLocation final {
    std::uint32_t line;
    std::uint32_t column;
};

template<>
struct std::formatter<SourceLocation> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }

    auto format(SourceLocation location, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}:{}", location.line, location.column);
    }
};

class LineIndex final {
public:
    explicit LineIndex(std::string_view text) noexcept;
    auto location(std::uint32_t offset) const noexcept -> SourceLocation;

private:
    std::uint32_t text_size;
    std::vector<std::uint32_t> line_starts;
};
