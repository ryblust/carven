export module zero.backend.emit;

import zero.frontend.token;
import zero.frontend.parser;
import std;

namespace {

constexpr auto format_tokens(std::span<const Token> tokens) noexcept -> std::string {
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
        result.append(tokens[i].lexeme);
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

constexpr auto emit_import(const ImportItem& item) noexcept -> std::string {
    auto result = std::string("import ").append(item.module_name).append(";\n");

    for (const auto& decl : item.using_decls) {
        result.append("using ").append(item.module_name)
                               .append("::").append(decl).append(";\n");
    }

    return result;
}

constexpr auto emit_enum(const EnumItem& item) noexcept -> std::string {
    auto result = std::string("enum class ").append(item.name).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(field).append(",\n");
    }

    return result.append("};\n");
}

constexpr auto emit_struct(const StructItem& item) noexcept -> std::string {
    auto result = std::string("struct ").append(item.name).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(map_type(field.type));
        result.push_back(' ');
        result.append(field.name).append(";\n");
    }

    return result.append("};\n");
}

constexpr auto emit_function(const FunctionItem& item) noexcept -> std::string {
    auto result = std::string("auto ").append(item.name).append("(");

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto& p = item.params[i];
        result.append(map_type(p.type));
        result.push_back(' ');
        result.append(p.name);
        if (i + 1 < item.params.size()) result.append(", ");
    }

    result.append(")");

    if (item.name == "main") {
        result.append(" -> int");
    } else if (item.return_type.empty()) {
        result.append(" -> void");
    } else {
        result.append(" -> ").append(map_type(item.return_type));
    }

    if (item.body_tokens.size() > 2) {
        const auto body_inner = item.body_tokens.subspan(1, item.body_tokens.size() - 2);
        result.append(" {\n    ").append(format_tokens(body_inner));
        result.push_back('\n');
    } else {
        result.append(" {\n");
    }

    return result.append("}\n");
}

} // namespace

export constexpr auto codegen(std::span<const TopLevelItem> items) noexcept -> std::string {
    auto result = std::string();

    for (const auto& item : items) {
        std::visit([&result](const auto& concrete) {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, ImportItem>)
                result.append(emit_import(concrete));
            else if constexpr (std::is_same_v<T, EnumItem>)
                result.append(emit_enum(concrete));
            else if constexpr (std::is_same_v<T, StructItem>)
                result.append(emit_struct(concrete));
            else if constexpr (std::is_same_v<T, FunctionItem>)
                result.append(emit_function(concrete));
        }, item);
        result.push_back('\n');
    }

    return result;
}
