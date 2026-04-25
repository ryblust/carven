export module zero.common.source;

import std;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;

    constexpr auto operator==(const Span&) const noexcept -> bool = default;
};

export struct SourceFile final {
    std::string_view filename;
    std::string_view content;

    constexpr auto text(Span span) const noexcept -> std::string_view {
        return content.substr(span.start, span.end - span.start);
    }
};
