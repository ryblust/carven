export module zero.common.source;

import std;

export template<class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };
export template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;
};

export constexpr auto is_empty(Span span) noexcept -> bool {
    return span.start >= span.end;
}

export struct SourceFile final {
    std::string filename;
    std::string content;
};

export constexpr auto text_at(std::string_view text, Span span) noexcept -> std::string_view {
    if (span.start > text.size() || span.end > text.size() || span.end < span.start) {
        return {};
    }
    return text.substr(span.start, span.end - span.start);
}

export struct LineColumn final {
    std::uint32_t line;
    std::uint32_t column;
};

export constexpr auto line_column_at(std::string_view text, std::uint32_t offset) noexcept -> LineColumn {
    auto line = 1u, last_newline = 0u;

    for (auto i = 0uz; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            last_newline = i + 1;
        }
    }

    return { .line = line, .column = offset - last_newline + 1 };
}
