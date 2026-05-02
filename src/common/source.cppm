export module zero.common.source;

import std;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;
};

export enum class CppStandard : std::uint8_t {
    Cpp14,
    Cpp17,
    Cpp20,
    Cpp23,
    Cpp26,
};

export constexpr auto display_cpp_standard(CppStandard standard) noexcept -> std::string_view {
    switch (standard) {
        using enum CppStandard;
        case Cpp14: return "c++14";
        case Cpp17: return "c++17";
        case Cpp20: return "c++20";
        case Cpp23: return "c++23";
        case Cpp26: return "c++26";
    }

    return "c++23";
}

export constexpr auto parse_cpp_standard(std::string_view value) noexcept -> std::optional<CppStandard> {
    if (value == "c++14") return CppStandard::Cpp14;
    if (value == "c++17") return CppStandard::Cpp17;
    if (value == "c++20") return CppStandard::Cpp20;
    if (value == "c++23") return CppStandard::Cpp23;
    if (value == "c++26") return CppStandard::Cpp26;

    return std::nullopt;
}

export struct SourceFile final {
    std::string_view filename;
    std::string_view content;
};

export constexpr auto text_at(std::string_view text, Span span) noexcept -> std::string_view {
    return text.substr(span.start, span.end - span.start);
}
