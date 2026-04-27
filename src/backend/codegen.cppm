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

} // namespace

export constexpr auto generate(std::span<const TopLevelItem> items, std::string_view source, [[maybe_unused]] TranspileOptions opts) noexcept -> std::string {
    auto result = std::string();

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
