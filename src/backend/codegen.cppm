export module carven.backend.codegen;

import carven.common.source;
import carven.frontend.ast;
import std;

constexpr auto map_type(std::string_view carven_type) noexcept -> std::string_view {
    if (carven_type == "i8")    return "std::int8_t";
    if (carven_type == "i16")   return "std::int16_t";
    if (carven_type == "i32")   return "std::int32_t";
    if (carven_type == "i64")   return "std::int64_t";
    if (carven_type == "u8")    return "std::uint8_t";
    if (carven_type == "u16")   return "std::uint16_t";
    if (carven_type == "u32")   return "std::uint32_t";
    if (carven_type == "u64")   return "std::uint64_t";
    if (carven_type == "bool")  return "bool";
    if (carven_type == "f32")   return "float";
    if (carven_type == "f64")   return "double";
    if (carven_type == "char")  return "char";
    if (carven_type == "usize") return "std::size_t";

    return carven_type;
}

constexpr auto generate_include_preamble(std::uint8_t standard) noexcept -> const char* {
    if (standard >= 26) return
        #include "headers/std26.inc"
    ;
    if (standard >= 23) return
        #include "headers/std23.inc"
    ;
    if (standard >= 20) return
        #include "headers/std20.inc"
    ;
    if (standard >= 17) return
        #include "headers/std17.inc"
    ;
    return
        #include "headers/base.inc"
    ;
}

class Codegen final {
public:
    constexpr Codegen(std::string_view source, std::uint8_t standard, bool default_include_std) noexcept
        : source(source), standard(standard), default_include_std(default_include_std) {
        result.reserve(source.size() * 2);
    }

    constexpr auto generate(std::span<const TopLevelItem> items) noexcept -> std::string {
        if (default_include_std || needs_standard_preamble(items)) {
            result += generate_include_preamble(standard);
            result += '\n';
        }

        for (const auto& item : items) {
            std::visit(Overloaded {
                [&](const ImportItem&   it) noexcept { generate_import(it);   },
                [&](const EnumItem&     it) noexcept { generate_enum(it);     },
                [&](const StructItem&   it) noexcept { generate_struct(it);   },
                [&](const FunctionItem& it) noexcept { generate_function(it); },
            }, item);
            result += "\n\n";
        }

        return std::move(result);
    }

private:
    std::string result;
    std::string_view source;
    std::uint8_t standard;
    bool default_include_std;
    std::uint32_t match_counter = 0;

    constexpr auto padding(std::uint32_t indent) const noexcept -> std::string {
        return std::string(indent, ' ');
    }

    constexpr auto next_match_temp() noexcept -> std::string {
        return std::string("__carven_match_") + std::to_string(match_counter++);
    }

    constexpr auto is_control_expr(const Expr& expr) noexcept -> bool {
        return expr.kind == ExprKind::If || expr.kind == ExprKind::Match;
    }

    constexpr auto is_main_args_function(const FunctionItem& item) const noexcept -> bool {
        return slice(source, item.name) == "main" && item.params.size() == 1 && item.params[0].type == nullptr;
    }

    constexpr auto needs_standard_preamble(std::span<const TopLevelItem> items) const noexcept -> bool {
        for (const auto& item : items) {
            if (const auto function = std::get_if<FunctionItem>(&item); function != nullptr && is_main_args_function(*function)) {
                return true;
            }
        }

        return false;
    }

    constexpr auto generate_type(const Type* type) noexcept -> void {
        if (type == nullptr) return;
        switch (type->kind) {
            case TypeKind::Name:
                result += map_type(slice(source, static_cast<const NameType*>(type)->span));
                break;
            case TypeKind::Array: {
                const auto arr = static_cast<const ArrayType*>(type);
                result += "std::array<";
                result += map_type(slice(source, arr->elem_type));
                result += ", ";
                const auto size = slice(source, arr->size);
                const auto first = size.find_first_not_of(" \t\n\r");
                const auto last  = size.find_last_not_of(" \t\n\r");
                result += (first != std::string_view::npos) ? size.substr(first, last - first + 1) : size;
                result += '>';
                break;
            }
        }
    }

    constexpr auto generate_import(const ImportItem& item) noexcept -> void {
        const auto name = slice(source, item.module_name);

        if (!item.is_std_module) {
            result += "import ";
            result += name;
            result += ";\n";
        }
        if (item.using_wildcard) {
            result += "using namespace ";
            result += name;
            result += ";\n";
        } else {
            for (const auto decl : item.using_decls) {
                result += "using ";
                result += name;
                result += "::";
                result += slice(source, decl);
                result += ";\n";
            }
        }
    }

    constexpr auto generate_enum(const EnumItem& item) noexcept -> void {
        result += "enum class ";
        result += slice(source, item.name);
        if (item.size != nullptr) {
            result += " : ";
            generate_type(item.size);
        }
        result += " {\n";

        for (const auto field : item.fields) {
            result += "    ";
            result += slice(source, field);
            result += ",\n";
        }

        result += "};";
    }

    constexpr auto generate_struct(const StructItem& item) noexcept -> void {
        result += "struct ";
        result += slice(source, item.name);
        result += " {\n";

        for (const auto field : item.fields) {
            result += "    ";
            generate_type(field.type);
            result += ' ';
            result += slice(source, field.name);
            result += ";\n";
        }

        result += "};";
    }

    constexpr auto generate_var_decl(const VarDecl& s, std::uint32_t indent = 0, std::string_view prefix = "") noexcept -> void {
        const auto keyword = slice(source, s.keyword);

        result += prefix;
        result += keyword == "let" ? "const auto " : keyword == "const" ? "constexpr auto " : "auto ";
        result += slice(source, s.name);

        if (s.init != nullptr) {
            if (s.type != nullptr) {
                result += " = ";
                generate_type(s.type);
                result += '(';
                generate_expr(*s.init, indent);
                result += ')';
            } else {
                result += " = ";
                generate_expr(*s.init, indent);
            }
        } else if (s.type != nullptr) {
            result += " = ";
            generate_type(s.type);
            result += "()";
        }
    }

    constexpr auto generate_function(const FunctionItem& item) noexcept -> void {
        const auto name = slice(source, item.name);

        if (is_main_args_function(item)) {
            generate_main_args_function(item);
            return;
        }

        result += "auto ";
        result += name;
        result += '(';

        for (auto i = 0uz; i < item.params.size(); ++i) {
            if (i > 0) result += ", ";
            const auto p = item.params[i];
            if (p.type == nullptr) {
                result += "auto ";
            } else {
                generate_type(p.type);
                result += ' ';
            }
            result += slice(source, p.name);
        }

        result += ") noexcept";

        if (name == "main") {
            result += " -> int";
        } else if (item.return_type != nullptr) {
            result += " -> ";
            generate_type(item.return_type);
        }

        result += " {\n";

        for (const auto stmt : item.body->statements) {
            generate_stmt(*stmt, 4);
            result += '\n';
        }

        result += '}';
    }

    constexpr auto generate_main_args_function(const FunctionItem& item) noexcept -> void {
        const auto args_name = slice(source, item.params[0].name);

        result += "auto main(int __carven_argc, char** __carven_argv) noexcept -> int {\n";
        result += "    const auto ";
        result += args_name;
        result += " =\n";
        result += "        std::views::iota(1, __carven_argc)\n";
        result += "        | std::views::transform([__carven_argv](int index) noexcept {\n";
        result += "              return std::pair<std::size_t, std::string_view> {\n";
        result += "                  static_cast<std::size_t>(index),\n";
        result += "                  std::string_view(__carven_argv[index]),\n";
        result += "              };\n";
        result += "          });\n";

        for (const auto stmt : item.body->statements) {
            generate_stmt(*stmt, 4);
            result += '\n';
        }

        result += '}';
    }

    constexpr auto generate_block_body(const BlockStmt& block, std::uint32_t indent) noexcept -> void {
        for (const auto stmt : block.statements) {
            generate_stmt(*stmt, indent);
            result += '\n';
        }
    }

    constexpr auto generate_stmt(const Stmt& stmt, std::uint32_t indent) noexcept -> void {
        const auto pad = padding(indent);

        switch (stmt.kind) {
            case StmtKind::Block: {
                result += pad;
                result += "{\n";
                generate_block_body(*static_cast<const BlockStmt*>(&stmt), indent + 4);
                result += pad;
                result += '}';
                return;
            }
            case StmtKind::ExprStmt: {
                const auto s = static_cast<const ExprStmt*>(&stmt);
                if (is_control_expr(*s->expr)) {
                    generate_control_stmt(*s->expr, indent);
                } else {
                    result += pad;
                    generate_expr(*s->expr, indent);
                    result += ';';
                }
                return;
            }
            case StmtKind::Empty:
                result += pad;
                result += ';';
                return;
            case StmtKind::VarDecl: {
                generate_var_decl(*static_cast<const VarDecl*>(&stmt), indent, pad);
                result += ';';
                return;
            }
            case StmtKind::Return: {
                const auto s = static_cast<const ReturnStmt*>(&stmt);
                result += pad;
                result += "return";
                if (s->value != nullptr) {
                    result += ' ';
                    generate_expr(*s->value, indent);
                }
                result += ';';
                return;
            }
            case StmtKind::While: {
                const auto s = static_cast<const WhileStmt*>(&stmt);
                result += pad;
                result += "while (";
                generate_expr(*s->condition, indent);
                result += ')';
                if (s->body->kind == StmtKind::Block) {
                    result += " {\n";
                    generate_block_body(*static_cast<const BlockStmt*>(s->body), indent + 4);
                    result += pad;
                    result += '}';
                } else {
                    result += '\n';
                    generate_stmt(*s->body, indent + 4);
                }
                return;
            }
            case StmtKind::For: {
                const auto s = static_cast<const ForStmt*>(&stmt);
                result += pad;
                result += "for (";
                std::visit(Overloaded {
                    [&](VarDecl* d) noexcept {
                        if (d != nullptr) generate_var_decl(*d);
                    },
                    [&](ExprStmt* es) noexcept {
                        if (es != nullptr) generate_expr(*es->expr, 0);
                    }
                }, s->init);
                result += "; ";
                if (s->condition != nullptr) generate_expr(*s->condition, 0);
                result += "; ";
                if (s->step != nullptr) generate_expr(*s->step, 0);
                result += ')';
                if (s->body->kind == StmtKind::Block) {
                    result += " {\n";
                    generate_block_body(*static_cast<const BlockStmt*>(s->body), indent + 4);
                    result += pad;
                    result += '}';
                } else {
                    result += '\n';
                    generate_stmt(*s->body, indent + 4);
                }
                return;
            }
        }
    }

    constexpr auto generate_control_stmt(const Expr& expr, std::uint32_t indent) noexcept -> void {
        if (expr.kind == ExprKind::If) {
            generate_if_stmt(*static_cast<const IfExpr*>(&expr), indent);
        } else {
            generate_match_stmt(*static_cast<const MatchExpr*>(&expr), indent);
        }
    }

    constexpr auto generate_expr(const Expr& expr, std::uint32_t indent) noexcept -> void {
        switch (expr.kind) {
            case ExprKind::Literal: {
                result += slice(source, static_cast<const LiteralExpr*>(&expr)->token);
                return;
            }
            case ExprKind::Ident: {
                result += slice(source, static_cast<const IdentExpr*>(&expr)->name);
                return;
            }
            case ExprKind::Prefix: {
                const auto e = static_cast<const PrefixExpr*>(&expr);
                result += slice(source, e->span);
                generate_expr(*e->rhs, indent);
                return;
            }
            case ExprKind::Postfix: {
                const auto e = static_cast<const PostfixExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += slice(source, e->span);
                return;
            }
            case ExprKind::Binary: {
                const auto e = static_cast<const BinaryExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += ' ';
                result += slice(source, e->span);
                result += ' ';
                generate_expr(*e->rhs, indent);
                return;
            }
            case ExprKind::Call: {
                const auto e = static_cast<const CallExpr*>(&expr);
                generate_expr(*e->callee, indent);
                result += '(';
                for (auto i = 0uz; i < e->args.size(); ++i) {
                    if (i > 0) result += ", ";
                    generate_expr(*e->args[i], indent);
                }
                result += ')';
                return;
            }
            case ExprKind::Index: {
                const auto e = static_cast<const IndexExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += '[';
                generate_expr(*e->index, indent);
                result += ']';
                return;
            }
            case ExprKind::Field: {
                const auto e = static_cast<const FieldExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += slice(source, e->dot);
                result += slice(source, e->field);
                return;
            }
            case ExprKind::Group: {
                result += '(';
                generate_expr(*static_cast<const GroupExpr*>(&expr)->inner, indent);
                result += ')';
                return;
            }
            case ExprKind::Assign: {
                const auto e = static_cast<const AssignExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += " = ";
                generate_expr(*e->rhs, indent);
                return;
            }
            case ExprKind::CompoundAssign: {
                const auto e = static_cast<const CompoundAssignExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += ' ';
                result += slice(source, e->span);
                result += ' ';
                generate_expr(*e->rhs, indent);
                return;
            }
            case ExprKind::Comma: {
                const auto e = static_cast<const CommaExpr*>(&expr);
                generate_expr(*e->lhs, indent);
                result += ", ";
                generate_expr(*e->rhs, indent);
                return;
            }
            case ExprKind::If:
                generate_if_expr(*static_cast<const IfExpr*>(&expr), indent);
                return;
            case ExprKind::Array: {
                const auto e = static_cast<const ArrayExpr*>(&expr);
                result += "std::array { ";
                for (auto i = 0uz; i < e->elements.size(); ++i) {
                    if (i > 0) result += ", ";
                    generate_expr(*e->elements[i], indent);
                }
                result += " }";
                return;
            }
            case ExprKind::Match:
                generate_match_expr(*static_cast<const MatchExpr*>(&expr), indent);
                return;
        }
    }

    constexpr auto generate_if_stmt(const IfExpr& e, std::uint32_t indent) noexcept -> void {
        const auto pad = padding(indent);
        result += pad;
        result += "if (";
        generate_expr(*e.condition, indent);
        result += ") {\n";
        generate_block_body(*e.then_branch, indent + 4);
        result += pad;
        result += '}';

        if (e.else_branch != nullptr) {
            result += " else {\n";
            generate_block_body(*e.else_branch, indent + 4);
            result += pad;
            result += '}';
        }
    }

    constexpr auto generate_if_expr(const IfExpr& e, std::uint32_t indent) noexcept -> void {
        const auto pad = padding(indent);
        result += "([&]() noexcept {\n";
        result += padding(indent + 4);
        result += "if (";
        generate_expr(*e.condition, indent + 4);
        result += ") {\n";
        generate_value_branch(*e.then_branch, indent + 8);
        result += padding(indent + 4);
        result += "} else {\n";
        generate_value_branch(*e.else_branch, indent + 8);
        result += padding(indent + 4);
        result += "}\n";
        result += pad;
        result += "}())";
    }

    constexpr auto generate_value_branch(const BlockStmt& block, std::uint32_t indent) noexcept -> void {
        if (block.statements.empty()) return;

        for (auto i = 0uz; i + 1 < block.statements.size(); ++i) {
            generate_stmt(*block.statements[i], indent);
            result += '\n';
        }

        const auto last = block.statements.back();
        if (last->kind != StmtKind::ExprStmt) {
            generate_stmt(*last, indent);
            result += '\n';
            return;
        }

        result += padding(indent);
        result += "return ";
        generate_expr(*static_cast<const ExprStmt*>(last)->expr, indent);
        result += ";\n";
    }

    constexpr auto arm_is_type(const MatchArm& arm) noexcept -> bool {
        return !arm.is_wildcard && !arm.patterns.empty() && arm.patterns[0]->kind == ExprKind::Ident;
    }

    constexpr auto generate_match_condition(const MatchArm& arm, std::string_view value_name, std::uint32_t indent) noexcept -> void {
        const auto is_type_arm = arm_is_type(arm);
        for (auto i = 0uz; i < arm.patterns.size(); ++i) {
            if (i > 0) result += " || ";
            if (is_type_arm) {
                result += "std::is_same_v<std::remove_cvref_t<decltype(";
                result += value_name;
                result += ")>, ";
                result += map_type(slice(source, static_cast<const IdentExpr*>(arm.patterns[i])->name));
                result += '>';
            } else {
                result += value_name;
                result += " == ";
                generate_expr(*arm.patterns[i], indent);
            }
        }
    }

    constexpr auto generate_match_arm_prefix(const MatchArm& arm, std::string_view value_name, std::uint32_t indent, bool first) noexcept -> void {
        if (!first) result += " else ";

        if (arm.is_wildcard) {
            result += "{\n";
            return;
        }

        result += arm_is_type(arm) ? "if constexpr (" : "if (";
        generate_match_condition(arm, value_name, indent);
        result += ") {\n";
    }

    constexpr auto generate_match_stmt(const MatchExpr& e, std::uint32_t indent) noexcept -> void {
        const auto pad = padding(indent);
        const auto value_name = next_match_temp();

        result += pad;
        result += "{\n";
        result += padding(indent + 4);
        result += "auto&& ";
        result += value_name;
        result += " = ";
        generate_expr(*e.value, indent + 4);
        result += ";\n";

        for (auto i = 0uz; i < e.arms.size(); ++i) {
            const auto& arm = e.arms[i];
            result += padding(indent + 4);
            generate_match_arm_prefix(arm, value_name, indent + 4, i == 0);
            generate_block_body(*arm.body, indent + 8);
            result += padding(indent + 4);
            result += '}';
            result += '\n';
        }

        result += pad;
        result += '}';
    }

    constexpr auto generate_match_expr(const MatchExpr& e, std::uint32_t indent) noexcept -> void {
        const auto pad = padding(indent);
        const auto value_name = next_match_temp();

        result += "([&]() noexcept {\n";
        result += padding(indent + 4);
        result += "auto&& ";
        result += value_name;
        result += " = ";
        generate_expr(*e.value, indent + 4);
        result += ";\n";

        for (auto i = 0uz; i < e.arms.size(); ++i) {
            const auto& arm = e.arms[i];
            result += padding(indent + 4);
            generate_match_arm_prefix(arm, value_name, indent + 4, i == 0);
            generate_value_branch(*arm.body, indent + 8);
            result += padding(indent + 4);
            result += '}';
            result += '\n';
        }

        result += pad;
        result += "}())";
    }
};

export constexpr auto generate(std::span<const TopLevelItem> items, std::string_view source, std::uint8_t standard, bool default_include_std) noexcept -> std::string {
    return Codegen(source, standard, default_include_std).generate(items);
}
