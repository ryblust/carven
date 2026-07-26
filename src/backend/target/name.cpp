module carven:backend.target.name.impl;

import :backend.target.name;
import :support.invariant;
import std;

namespace {

constexpr auto target_keywords = std::array {
    std::string_view("alignas"),
    std::string_view("alignof"),
    std::string_view("and"),
    std::string_view("and_eq"),
    std::string_view("asm"),
    std::string_view("auto"),
    std::string_view("bitand"),
    std::string_view("bitor"),
    std::string_view("bool"),
    std::string_view("break"),
    std::string_view("case"),
    std::string_view("catch"),
    std::string_view("char"),
    std::string_view("char8_t"),
    std::string_view("char16_t"),
    std::string_view("char32_t"),
    std::string_view("class"),
    std::string_view("compl"),
    std::string_view("concept"),
    std::string_view("const"),
    std::string_view("consteval"),
    std::string_view("constexpr"),
    std::string_view("constinit"),
    std::string_view("const_cast"),
    std::string_view("continue"),
    std::string_view("co_await"),
    std::string_view("co_return"),
    std::string_view("co_yield"),
    std::string_view("decltype"),
    std::string_view("default"),
    std::string_view("delete"),
    std::string_view("do"),
    std::string_view("double"),
    std::string_view("dynamic_cast"),
    std::string_view("else"),
    std::string_view("enum"),
    std::string_view("explicit"),
    std::string_view("export"),
    std::string_view("extern"),
    std::string_view("false"),
    std::string_view("float"),
    std::string_view("for"),
    std::string_view("friend"),
    std::string_view("goto"),
    std::string_view("if"),
    std::string_view("import"),
    std::string_view("inline"),
    std::string_view("int"),
    std::string_view("long"),
    std::string_view("module"),
    std::string_view("mutable"),
    std::string_view("namespace"),
    std::string_view("new"),
    std::string_view("noexcept"),
    std::string_view("not"),
    std::string_view("not_eq"),
    std::string_view("nullptr"),
    std::string_view("operator"),
    std::string_view("or"),
    std::string_view("or_eq"),
    std::string_view("private"),
    std::string_view("protected"),
    std::string_view("public"),
    std::string_view("register"),
    std::string_view("reinterpret_cast"),
    std::string_view("requires"),
    std::string_view("return"),
    std::string_view("short"),
    std::string_view("signed"),
    std::string_view("sizeof"),
    std::string_view("static"),
    std::string_view("static_assert"),
    std::string_view("static_cast"),
    std::string_view("struct"),
    std::string_view("switch"),
    std::string_view("template"),
    std::string_view("this"),
    std::string_view("thread_local"),
    std::string_view("throw"),
    std::string_view("true"),
    std::string_view("try"),
    std::string_view("typedef"),
    std::string_view("typeid"),
    std::string_view("typename"),
    std::string_view("union"),
    std::string_view("unsigned"),
    std::string_view("using"),
    std::string_view("virtual"),
    std::string_view("void"),
    std::string_view("volatile"),
    std::string_view("wchar_t"),
    std::string_view("while"),
    std::string_view("xor"),
    std::string_view("xor_eq"),
};

constexpr auto ascii_alpha(char value) noexcept -> bool {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

constexpr auto ascii_digit(char value) noexcept -> bool {
    return value >= '0' && value <= '9';
}

} // namespace

auto TargetIdentifier::accepts_spelling(std::string_view spelling) noexcept -> bool {
    if (spelling.empty() || !(ascii_alpha(spelling.front()) || spelling.front() == '_')) {
        return false;
    }
    if (std::ranges::any_of(spelling.substr(1), [](char value) static noexcept -> bool {
            return !(ascii_alpha(value) || ascii_digit(value) || value == '_');
        })) {
        return false;
    }
    return std::ranges::find(target_keywords, spelling) == target_keywords.end();
}

auto TargetIdentifier::from_spelling(std::string_view spelling) noexcept -> TargetIdentifier {
    if (!accepts_spelling(spelling)) {
        invariant_violation(std::format("target identifier spelling '{}' is invalid", spelling));
    }
    return TargetIdentifier(std::string(spelling));
}

TargetIdentifier::TargetIdentifier(std::string spelling) noexcept
    : value(std::move(spelling)) {}

auto TargetIdentifier::spelling() const noexcept -> std::string_view {
    return value;
}

TargetName::TargetName(TargetIdentifier identifier) noexcept
    : TargetName(std::vector<TargetIdentifier> {std::move(identifier)}, false) {}

auto TargetName::from_components(std::initializer_list<TargetIdentifier> values) noexcept
    -> TargetName {
    return TargetName(std::vector<TargetIdentifier>(values), false);
}

auto TargetName::from_components(std::vector<TargetIdentifier> values) noexcept -> TargetName {
    return TargetName(std::move(values), false);
}

auto TargetName::globally_qualified(std::vector<TargetIdentifier> values) noexcept -> TargetName {
    return TargetName(std::move(values), true);
}

TargetName::TargetName(std::vector<TargetIdentifier> components, bool globally_qualified) noexcept
    : name_components(std::move(components)),
      global_qualification(globally_qualified) {
    if (name_components.empty()) {
        invariant_violation("target qualified name requires at least one component");
    }
}

auto TargetName::components() const noexcept -> std::span<const TargetIdentifier> {
    return name_components;
}

auto TargetName::is_globally_qualified() const noexcept -> bool {
    return global_qualification;
}

auto TargetName::append(TargetIdentifier identifier) noexcept -> void {
    name_components.push_back(std::move(identifier));
}
