export module zero.common.source;

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

export auto is_empty(Span span) noexcept -> bool;

export auto text_at(std::string_view text, Span span) noexcept -> std::string_view;

export auto location_at(std::string_view text, std::uint32_t offset) noexcept -> SourceLocation;
