module zero.common.source;

import std;

auto is_empty(Span span) noexcept -> bool {
    return span.start >= span.end;
}

auto text_at(std::string_view text, Span span) noexcept -> std::string_view {
    if (span.start > text.size() || span.end > text.size() || span.end < span.start) {
        return {};
    }
    return text.substr(span.start, span.end - span.start);
}

auto location_at(std::string_view text, std::uint32_t offset) noexcept -> SourceLocation {
    auto line = 1u, last_newline = 0u;

    for (auto i = 0uz; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            last_newline = i + 1;
        }
    }

    return { .line = line, .column = offset - last_newline + 1 };
}
