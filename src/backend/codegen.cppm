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
        result.append(text_at(source, tokens[i].span));
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

    return zero_type;
}

constexpr auto generate_import(const ImportItem& item, std::string_view source) noexcept -> std::string {
    const auto mod = text_at(source, item.module_name);
    auto result = std::string("import ").append(mod).append(";\n");

    for (const auto& decl : item.using_decls) {
        result.append("using ")
              .append(mod)
              .append("::").append(text_at(source, decl)).append(";\n");
    }

    return result;
}

constexpr auto generate_enum(const EnumItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("enum class ").append(text_at(source, item.name)).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(text_at(source, field)).append(",\n");
    }

    return result.append("};\n");
}

constexpr auto generate_struct(const StructItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("struct ").append(text_at(source, item.name)).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(map_type(text_at(source, field.type)));
        result.push_back(' ');
        result.append(text_at(source, field.name)).append(";\n");
    }

    return result.append("};\n");
}

constexpr auto generate_function(const FunctionItem& item, std::string_view source) noexcept -> std::string {
    const auto name = text_at(source, item.name);
    auto result = std::string("auto ").append(name).append("(");

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto& p = item.params[i];
        result.append(map_type(text_at(source, p.type)));
        result.push_back(' ');
        result.append(text_at(source, p.name));

        if (i + 1 < item.params.size()) result.append(", ");
    }

    result.append(")");

    if (name == "main") {
        result.append(" -> int");
    } else if (item.return_type.start == 0 && item.return_type.end == 0) {
        result.append(" -> void");
    } else {
        result.append(" -> ").append(map_type(text_at(source, item.return_type)));
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

constexpr auto CPP_HEADERS_BASE = std::string_view {
    #include "headers/base.inc"
};

constexpr auto CPP_HEADERS_17 = std::string_view {
    #include "headers/std17.inc"
};

constexpr auto CPP_HEADERS_20 = std::string_view {
    #include "headers/std20.inc"
};

constexpr auto CPP_HEADERS_23 = std::string_view {
    #include "headers/std23.inc"
};

constexpr auto CPP_HEADERS_26 = std::string_view {
    #include "headers/std26.inc"
};

constexpr auto is_std_import(std::span<const TopLevelItem> items, std::string_view source) noexcept -> bool {
    for (const auto& item : items) {
        if (std::holds_alternative<ImportItem>(item)) {
            if (const auto& mod = std::get<ImportItem>(item);
                    text_at(source, mod.module_name) == "std") {
                return true;
            }
        }
    }

    return false;
}

constexpr auto generate_include_preamble(CppStandard standard) noexcept -> std::string {
    auto result = std::string(CPP_HEADERS_BASE);

    if (standard >= CppStandard::Cpp17) result.append(CPP_HEADERS_17);
    if (standard >= CppStandard::Cpp20) result.append(CPP_HEADERS_20);
    if (standard >= CppStandard::Cpp23) result.append(CPP_HEADERS_23);
    if (standard >= CppStandard::Cpp26) result.append(CPP_HEADERS_26);

    return result.append("\n");
}

constexpr auto generate_import_preamble() noexcept -> std::string {
    return std::string("import std;\n\n");
}

template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

export constexpr auto generate(std::span<const TopLevelItem> items, std::string_view source, TranspileOptions opts) noexcept -> std::string {
    auto result = std::string();

    if (opts.standard <= CppStandard::Cpp17) {
        if (opts.default_include_std) {
            result.append(generate_include_preamble(opts.standard));
        }
    } else {
        if (opts.default_import_std && !is_std_import(items, source)) {
            result.append(generate_import_preamble());
        }
    }

    for (const auto& item : items) {
        std::visit(Overloaded {
            [&](const   ImportItem& it) { result.append(generate_import  (it, source)); },
            [&](const     EnumItem& it) { result.append(generate_enum    (it, source)); },
            [&](const   StructItem& it) { result.append(generate_struct  (it, source)); },
            [&](const FunctionItem& it) { result.append(generate_function(it, source)); },
        }, item);
        result.push_back('\n');
    }

    return result;
}
