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

export struct TranspileOptions final {
    CppStandard standard = CppStandard::Cpp20;
    bool default_include_std = true;  // emit #include for all std headers (C++17 and below)
    bool default_import_std  = true;  // emit import std; (C++20 and above)
};

export struct SourceFile final {
    std::string_view filename;
    std::string_view content;
};

export constexpr auto text_at(std::string_view text, Span span) noexcept -> std::string_view {
    return text.substr(span.start, span.end - span.start);
}
