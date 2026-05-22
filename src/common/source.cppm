export module carven.common.source;

import std;

export template<typename ... Ts>
struct Overloaded final : Ts... { using Ts::operator()...; };

export template<typename ... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;
};

export struct SourceFile final {
    std::string filename;
    std::string content;
};

export struct SourceLocation final {
    std::uint32_t line;
    std::uint32_t column;
};

template<> struct std::formatter<SourceLocation> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    auto format(SourceLocation location, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}:{}", location.line, location.column);
    }
};

export constexpr auto is_empty(Span span) noexcept -> bool {
    return span.start >= span.end;
}

export constexpr auto text_at(std::string_view text, Span span) noexcept -> std::string_view {
    if (span.start > text.size() || span.end > text.size() || span.end < span.start) {
        return {};
    }

    return text.substr(span.start, span.end - span.start);
}

export constexpr auto location_at(std::string_view text, std::uint32_t offset) noexcept -> SourceLocation {
    auto line = 1u, last_newline = 0u;

    for (auto i = 0uz; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            last_newline = i + 1;
        }
    }

    return { .line = line, .column = offset - last_newline + 1 };
}
