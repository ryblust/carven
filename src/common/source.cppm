export module zero.common.source;

import std;

export struct Span final {
    std::uint32_t start;
    std::uint32_t end;

    constexpr auto operator==(const Span&) const noexcept -> bool = default;
};

export enum class CppStandard : std::uint8_t {
    Cpp14,
    Cpp17,
    Cpp20,
    Cpp23,
    Cpp26,
};

export struct TranspileOptions final {
    CppStandard standard = CppStandard::Cpp20;
    bool default_include_std = true;  // emit #include for all std headers (C++17 and below)
    bool default_import_std  = true;  // emit import std; (C++20 and above)
};

export struct SourceFile final {
    std::string_view filename;
    std::string_view content;

    constexpr auto text(Span span) const noexcept -> std::string_view {
        return content.substr(span.start, span.end - span.start);
    }
};
