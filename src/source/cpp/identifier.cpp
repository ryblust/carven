module carven:source.cpp.identifier.impl;

import :source.cpp.identifier;
import std;

namespace {

constexpr auto cpp_keywords = std::to_array<std::string_view>({
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "import",
    "inline",
    "int",
    "long",
    "module",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
});

constexpr auto ascii_alpha(char value) noexcept -> bool {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

constexpr auto ascii_digit(char value) noexcept -> bool {
    return value >= '0' && value <= '9';
}

} // namespace

auto is_supported_cpp_identifier(std::string_view spelling) noexcept -> bool {
    if (spelling.empty() || !(ascii_alpha(spelling.front()) || spelling.front() == '_')) {
        return false;
    }
    if (std::ranges::any_of(spelling.substr(1), [](char value) static noexcept {
            return !(ascii_alpha(value) || ascii_digit(value) || value == '_');
        })) {
        return false;
    }
    return !std::ranges::contains(cpp_keywords, spelling);
}
