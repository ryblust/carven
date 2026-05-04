export module zero.common.source;

import std;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;
};

export struct SourceFile final {
    std::string filename;
    std::string content;
};

export constexpr auto text_at(std::string_view text, Span span) noexcept -> std::string_view {
    return text.substr(span.start, span.end - span.start);
}
