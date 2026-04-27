export module zero.backend.codegen;

import zero.common.source;
import zero.frontend.token;
import zero.frontend.parser;
import std;

namespace {

constexpr auto format_tokens(std::span<const Token> tokens, std::string_view source) noexcept -> std::string {
    auto result = std::string();

    for (auto i = 0uz; i < tokens.size(); ++i) {
        if (i > 0) {
            const auto prev = tokens[i - 1].kind;
            const auto curr = tokens[i].kind;
            if (curr != TokenKind::SemiColon
                && curr != TokenKind::Comma
                && curr != TokenKind::LeftParen
                && curr != TokenKind::RightParen
                && curr != TokenKind::RightBrace
                && curr != TokenKind::RightBracket
                && prev != TokenKind::LeftParen
                && prev != TokenKind::LeftBrace
                && prev != TokenKind::LeftBracket) {
                result.push_back(' ');
            }
        }
        result.append(source.substr(tokens[i].span.start, tokens[i].span.end - tokens[i].span.start));
    }

    return result;
}

constexpr auto map_type(std::string_view zero_type) noexcept -> std::string_view {
    if (zero_type == "i8")   return "std::int8_t";
    if (zero_type == "i16")  return "std::int16_t";
    if (zero_type == "i32")  return "std::int32_t";
    if (zero_type == "i64")  return "std::int64_t";
    if (zero_type == "u8")   return "std::uint8_t";
    if (zero_type == "u16")  return "std::uint16_t";
    if (zero_type == "u32")  return "std::uint32_t";
    if (zero_type == "u64")  return "std::uint64_t";
    if (zero_type == "bool") return "bool";
    if (zero_type == "f32")  return "float";
    if (zero_type == "f64")  return "double";
    if (zero_type == "char") return "char";
    if (zero_type == "str")  return "std::string_view";

    return zero_type;
}

constexpr auto generate_import(const ImportItem& item, std::string_view source) noexcept -> std::string {
    const auto mod = source.substr(item.module_name.start, item.module_name.end - item.module_name.start);
    auto result = std::string("import ").append(mod).append(";\n");

    for (const auto& decl : item.using_decls) {
        result.append("using ")
              .append(mod)
              .append("::").append(source.substr(decl.start, decl.end - decl.start)).append(";\n");
    }

    return result;
}

constexpr auto generate_enum(const EnumItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("enum class ").append(source.substr(item.name.start, item.name.end - item.name.start)).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(source.substr(field.start, field.end - field.start)).append(",\n");
    }

    return result.append("};\n");
}

constexpr auto generate_struct(const StructItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("struct ").append(source.substr(item.name.start, item.name.end - item.name.start)).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(map_type(source.substr(field.type.start, field.type.end - field.type.start)));
        result.push_back(' ');
        result.append(source.substr(field.name.start, field.name.end - field.name.start)).append(";\n");
    }

    return result.append("};\n");
}

constexpr auto generate_function(const FunctionItem& item, std::string_view source) noexcept -> std::string {
    const auto name = source.substr(item.name.start, item.name.end - item.name.start);
    auto result = std::string("auto ").append(name).append("(");

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto& p = item.params[i];
        result.append(map_type(source.substr(p.type.start, p.type.end - p.type.start)));
        result.push_back(' ');
        result.append(source.substr(p.name.start, p.name.end - p.name.start));
        if (i + 1 < item.params.size()) result.append(", ");
    }

    result.append(")");

    if (name == "main") {
        result.append(" -> int");
    } else if (item.return_type == Span{}) {
        result.append(" -> void");
    } else {
        result.append(" -> ").append(map_type(source.substr(item.return_type.start, item.return_type.end - item.return_type.start)));
    }

    if (item.body_tokens.size() > 2) {
        const auto body_inner = item.body_tokens.subspan(1, item.body_tokens.size() - 2);
        result.append(" {\n    ").append(format_tokens(body_inner, source));
        result.push_back('\n');
    } else {
        result.append(" {\n");
    }

    return result.append("}\n");
}

constexpr auto CPP_HEADERS_BASE = std::array {
    "algorithm", "array", "atomic", "bitset",
    "chrono", "complex", "condition_variable",
    "deque", "exception", "forward_list", "fstream", "functional", "future",
    "initializer_list", "iomanip", "ios", "iosfwd", "iostream", "istream", "iterator",
    "limits", "list", "locale",
    "map", "memory", "mutex",
    "new", "numeric",
    "ostream",
    "queue",
    "random", "ratio", "regex",
    "scoped_allocator", "set", "shared_mutex", "sstream", "stack", "stdexcept", "streambuf", "string", "strstream", "system_error",
    "thread", "tuple", "type_traits", "typeindex", "typeinfo",
    "unordered_map", "unordered_set", "utility",
    "valarray", "variant", "vector",
    "cassert", "cctype", "cerrno", "cfenv", "cfloat", "cinttypes", "climits", "clocale", "cmath",
    "csetjmp", "csignal", "cstdarg", "cstddef", "cstdint", "cstdio", "cstdlib", "cstring", "ctime",
    "cuchar", "cwchar", "cwctype",
};

constexpr auto CPP_HEADERS_17 = std::array {
    "any", "charconv", "execution", "filesystem", "memory_resource", "optional", "string_view", "variant",
};

constexpr auto CPP_HEADERS_20 = std::array {
    "barrier", "bit", "compare", "concepts", "coroutine",
    "format", "numbers", "ranges", "semaphore", "source_location",
    "span", "spanstream", "stop_token", "syncstream", "version",
};

constexpr auto CPP_HEADERS_23 = std::array {
    "expected", "flat_map", "flat_set", "generator", "mdspan", "print", "stacktrace", "stdfloat",
};

constexpr auto CPP_HEADERS_26 = std::array {
    "hazard_pointer", "inplace_vector", "linalg", "rcu", "simd",
};

constexpr auto is_std_import(std::span<const TopLevelItem> items, std::string_view source) noexcept -> bool {
    for (const auto& item : items) {
        if (std::holds_alternative<ImportItem>(item)) {
            const auto& imp = std::get<ImportItem>(item);
            if (source.substr(imp.module_name.start, imp.module_name.end - imp.module_name.start) == "std")
                return true;
        }
    }
    return false;
}

constexpr auto generate_include_preamble(CppStandard standard) noexcept -> std::string {
    auto result = std::string();
    for (const auto header : CPP_HEADERS_BASE)
        result.append("#include <").append(header).append(">\n");
    if (standard >= CppStandard::Cpp17)
        for (const auto header : CPP_HEADERS_17)
            result.append("#include <").append(header).append(">\n");
    if (standard >= CppStandard::Cpp20)
        for (const auto header : CPP_HEADERS_20)
            result.append("#include <").append(header).append(">\n");
    if (standard >= CppStandard::Cpp23)
        for (const auto header : CPP_HEADERS_23)
            result.append("#include <").append(header).append(">\n");
    if (standard >= CppStandard::Cpp26)
        for (const auto header : CPP_HEADERS_26)
            result.append("#include <").append(header).append(">\n");
    result.push_back('\n');
    return result;
}

constexpr auto generate_import_preamble() noexcept -> std::string {
    return std::string("import std;\n\n");
}

} // namespace

export constexpr auto generate(std::span<const TopLevelItem> items, std::string_view source, TranspileOptions opts) noexcept -> std::string {
    auto result = std::string();

    if (opts.standard <= CppStandard::Cpp17) {
        if (opts.default_include_std)
            result.append(generate_include_preamble(opts.standard));
    } else {
        if (opts.default_import_std && !is_std_import(items, source))
            result.append(generate_import_preamble());
    }

    for (const auto& item : items) {
        std::visit([&result, source](const auto& concrete) {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, ImportItem>)
                result.append(generate_import(concrete, source));
            else if constexpr (std::is_same_v<T, EnumItem>)
                result.append(generate_enum(concrete, source));
            else if constexpr (std::is_same_v<T, StructItem>)
                result.append(generate_struct(concrete, source));
            else if constexpr (std::is_same_v<T, FunctionItem>)
                result.append(generate_function(concrete, source));
        }, item);
        result.push_back('\n');
    }

    return result;
}
