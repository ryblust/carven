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

constexpr auto generate_type(std::string& result, Span type_span, const SourceFile& source) noexcept -> void {
    if (type_span.empty()) return;
    const auto text = source.slice(type_span);
    if (text.starts_with('[')) {
        const auto semicolon = text.find(';');
        if (semicolon == std::string_view::npos) {
            result += text; // fallback: shouldn't happen for valid array types
            return;
        }
        const auto inner_type = text.substr(1, semicolon - 1);
        const auto size       = text.substr(semicolon + 1, text.size() - semicolon - 2);
        result += "std::array<";
        result += map_type(inner_type);
        result += ", ";
        // trim whitespace from size
        const auto first = size.find_first_not_of(" \t\n\r");
        const auto last  = size.find_last_not_of(" \t\n\r");
        if (first != std::string_view::npos) {
            result += size.substr(first, last - first + 1);
        } else {
            result += size;
        }
        result += '>';
    } else {
        result += map_type(text);
    }
}

constexpr auto generate_import(std::string& result, const ImportItem& item, const SourceFile& source) noexcept -> void {
    const auto name = source.slice(item.module_name);

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
            result += source.slice(decl);
            result += ";\n";
        }
    }
}

constexpr auto generate_enum(std::string& result, const EnumItem& item, const SourceFile& source) noexcept -> void {
    result += "enum class ";
    result += source.slice(item.name);
    if (!item.size.empty()) {
        result += " : ";
        generate_type(result, item.size, source);
    }
    result += " {\n";

    for (const auto field : item.fields) {
        result += "    ";
        result += source.slice(field);
        result += ",\n";
    }

    result += "};";
}

constexpr auto generate_struct(std::string& result, const StructItem& item, const SourceFile& source) noexcept -> void {
    result += "struct ";
    result += source.slice(item.name);
    result += " {\n";

    for (const auto field : item.fields) {
        result += "    ";
        generate_type(result, field.type, source);
        result += ' ';
        result += source.slice(field.name);
        result += ";\n";
    }

    result += "};";
}

constexpr auto generate_stmt(std::string& result, const Stmt& stmt, const SourceFile& source, std::uint32_t indent) noexcept -> void;
constexpr auto generate_expr(std::string& result, const Expr& expr, const SourceFile& source) noexcept -> void;

constexpr auto generate_var_decl(std::string& result, const VarDecl& s, const SourceFile& source, std::string_view padding = "") noexcept -> void {
    const auto keyword = source.slice(s.keyword);

    result += padding;
    result += keyword == "let" ? "const auto " : keyword == "const" ? "constexpr auto " : "auto ";
    result += source.slice(s.name);

    if (s.init != nullptr) {
        if (!s.type.empty()) {
            result += " = ";
            generate_type(result, s.type, source);
            result += '(';
            generate_expr(result, *s.init, source);
            result += ')';
        } else {
            result += " = ";
            generate_expr(result, *s.init, source);
        }
    } else if (!s.type.empty()) {
        result += " = ";
        generate_type(result, s.type, source);
        result += "()";
    }
}

constexpr auto generate_function(std::string& result, const FunctionItem& item, const SourceFile& source) noexcept -> void {
    const auto name = source.slice(item.name);

    result += "auto ";
    result += name;
    result += '(';

    for (auto i = 0uz; i < item.params.size(); ++i) {
        if (i > 0) result += ", ";
        const auto p = item.params[i];
        if (p.type.empty()) {
            result += "auto ";
        } else {
            generate_type(result, p.type, source);
            result += ' ';
        }
        result += source.slice(p.name);
    }

    result += ") noexcept";

    if (name == "main") {
        result += " -> int";
    } else if (!item.return_type.empty()) {
        result += " -> ";
        generate_type(result, item.return_type, source);
    }

    result += " {\n";

    for (const auto stmt : item.body->statements) {
        generate_stmt(result, *stmt, source, 4);
        result += '\n';
    }

    result += '}';
}

constexpr auto generate_stmt(std::string& result, const Stmt& stmt, const SourceFile& source, std::uint32_t indent) noexcept -> void {
    const auto padding = std::string(indent, ' ');

    switch (stmt.kind) {
        case StmtKind::Block: {
            const auto& s = *static_cast<const BlockStmt*>(&stmt);
            result += padding;
            result += "{\n";
            for (const auto body_stmt : s.statements) {
                generate_stmt(result, *body_stmt, source, indent + 4);
                result += '\n';
            }
            result += padding;
            result += '}';
            return;
        }
        case StmtKind::ExprStmt: {
            const auto& s = *static_cast<const ExprStmt*>(&stmt);
            result += padding;
            generate_expr(result, *s.expr, source);
            result += ';';
            return;
        }
        case StmtKind::Empty:
            result += padding;
            result += ';';
            return;
        case StmtKind::VarDecl: {
            const auto& s = *static_cast<const VarDecl*>(&stmt);
            generate_var_decl(result, s, source, padding);
            result += ';';
            return;
        }
        case StmtKind::Return: {
            const auto& s = *static_cast<const ReturnStmt*>(&stmt);
            result += padding;
            result += "return";
            if (s.value != nullptr) {
                result += ' ';
                generate_expr(result, *s.value, source);
            }
            result += ';';
            return;
        }
        case StmtKind::While: {
            const auto& s = *static_cast<const WhileStmt*>(&stmt);
            result += padding;
            result += "while (";
            generate_expr(result, *s.condition, source);
            result += ')';
            if (s.body->kind == StmtKind::Block) {
                result += " {\n";
                for (const auto body_stmt : static_cast<const BlockStmt*>(s.body)->statements) {
                    generate_stmt(result, *body_stmt, source, indent + 4);
                    result += '\n';
                }
                result += padding;
                result += '}';
            } else {
                result += '\n';
                generate_stmt(result, *s.body, source, indent + 4);
            }
            return;
        }
        case StmtKind::For: {
            const auto& s = *static_cast<const ForStmt*>(&stmt);
            result += padding;
            result += "for (";
            std::visit(Overloaded {
                [&](VarDecl* d) noexcept {
                    if (d) generate_var_decl(result, *d, source);
                },
                [&](ExprStmt* es) noexcept {
                    if (es) generate_expr(result, *es->expr, source);
                }
            }, s.init);
            result += "; ";
            if (s.condition != nullptr) generate_expr(result, *s.condition, source);
            result += "; ";
            if (s.step != nullptr) generate_expr(result, *s.step, source);
            result += ')';
            if (s.body->kind == StmtKind::Block) {
                result += " {\n";
                for (const auto body_stmt : static_cast<const BlockStmt*>(s.body)->statements) {
                    generate_stmt(result, *body_stmt, source, indent + 4);
                    result += '\n';
                }
                result += padding;
                result += '}';
            } else {
                result += '\n';
                generate_stmt(result, *s.body, source, indent + 4);
            }
            return;
        }
    }
}

constexpr auto generate_expr(std::string& result, const Expr& expr, const SourceFile& source) noexcept -> void {
    switch (expr.kind) {
        case ExprKind::Literal: {
            const auto& e = *static_cast<const LiteralExpr*>(&expr);
            result += source.slice(e.token);
            return;
        }
        case ExprKind::Ident: {
            const auto& e = *static_cast<const IdentExpr*>(&expr);
            result += source.slice(e.name);
            return;
        }
        case ExprKind::Prefix: {
            const auto& e = *static_cast<const PrefixExpr*>(&expr);
            result += source.slice(e.span);
            generate_expr(result, *e.rhs, source);
            return;
        }
        case ExprKind::Postfix: {
            const auto& e = *static_cast<const PostfixExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += source.slice(e.span);
            return;
        }
        case ExprKind::Binary: {
            const auto& e = *static_cast<const BinaryExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += ' ';
            result += source.slice(e.span);
            result += ' ';
            generate_expr(result, *e.rhs, source);
            return;
        }
        case ExprKind::Call: {
            const auto& e = *static_cast<const CallExpr*>(&expr);
            generate_expr(result, *e.callee, source);
            result += '(';
            for (auto i = 0uz; i < e.args.size(); ++i) {
                if (i > 0) result += ", ";
                generate_expr(result, *e.args[i], source);
            }
            result += ')';
            return;
        }
        case ExprKind::Index: {
            const auto& e = *static_cast<const IndexExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += '[';
            generate_expr(result, *e.index, source);
            result += ']';
            return;
        }
        case ExprKind::Field: {
            const auto& e = *static_cast<const FieldExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += source.slice(e.dot);
            result += source.slice(e.field);
            return;
        }
        case ExprKind::Group: {
            const auto& e = *static_cast<const GroupExpr*>(&expr);
            result += '(';
            generate_expr(result, *e.inner, source);
            result += ')';
            return;
        }
        case ExprKind::Assign: {
            const auto& e = *static_cast<const AssignExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += " = ";
            generate_expr(result, *e.rhs, source);
            return;
        }
        case ExprKind::CompoundAssign: {
            const auto& e = *static_cast<const CompoundAssignExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += ' ';
            result += source.slice(e.span);
            result += ' ';
            generate_expr(result, *e.rhs, source);
            return;
        }
        case ExprKind::Comma: {
            const auto& e = *static_cast<const CommaExpr*>(&expr);
            generate_expr(result, *e.lhs, source);
            result += ", ";
            generate_expr(result, *e.rhs, source);
            return;
        }
        case ExprKind::If: {
            const auto& e = *static_cast<const IfExpr*>(&expr);

            const auto then_expr = e.then_branch->statements.size() == 1
                ? static_cast<const ExprStmt*>(e.then_branch->statements[0])
                : nullptr;
            const auto then_is_expr = then_expr != nullptr && then_expr->kind == StmtKind::ExprStmt;
            const auto else_expr = e.else_branch && e.else_branch->statements.size() == 1
                ? static_cast<const ExprStmt*>(e.else_branch->statements[0])
                : nullptr;
            const auto else_is_expr = else_expr != nullptr && else_expr->kind == StmtKind::ExprStmt;

            if (then_is_expr && else_is_expr) {
                generate_expr(result, *e.condition, source);
                result += " ? ";
                generate_expr(result, *then_expr->expr, source);
                result += " : ";
                generate_expr(result, *else_expr->expr, source);
                return;
            }

            result += "if (";
            generate_expr(result, *e.condition, source);
            result += ") {\n";
            for (const auto stmt : e.then_branch->statements) {
                generate_stmt(result, *stmt, source, 4);
                result += '\n';
            }
            result += '}';

            if (e.else_branch) {
                result += " else {\n";
                for (const auto stmt : e.else_branch->statements) {
                    generate_stmt(result, *stmt, source, 4);
                    result += '\n';
                }
                result += '}';
            }

            return;
        }
        case ExprKind::Array: {
            const auto& e = *static_cast<const ArrayExpr*>(&expr);
            result += "std::array { ";
            for (auto i = 0uz; i < e.elements.size(); ++i) {
                if (i > 0) result += ", ";
                generate_expr(result, *e.elements[i], source);
            }
            result += " }";
            return;
        }
        case ExprKind::Match: {
            const auto& e = *static_cast<const MatchExpr*>(&expr);
            if (e.arms.empty()) return;

            // Check if any arm has type patterns
            auto has_type = false;
            for (const auto& arm : e.arms) {
                if (!arm.is_wildcard && !arm.patterns.empty()
                    && arm.patterns[0]->kind == ExprKind::Ident) {
                    has_type = true;
                    break;
                }
            }

            if (has_type) {
                // Emit match value into _cv, compute _T for type checks
                result += "auto&& _cv = ";
                generate_expr(result, *e.value, source);
                result += ";\n";
                result += "using _T = std::remove_cvref_t<decltype(_cv)>;\n";

                auto else_depth = 0u;
                for (auto i = 0uz; i < e.arms.size(); ++i) {
                    const auto& arm = e.arms[i];

                    if (i > 0) {
                        result += " else {\n";
                        ++else_depth;
                    }

                    if (!arm.is_wildcard) {
                        // Determine if this arm is a type pattern
                        const auto is_type_arm = !arm.patterns.empty()
                            && arm.patterns[0]->kind == ExprKind::Ident;

                        result += is_type_arm ? "if constexpr (" : "if (";
                        for (auto j = 0uz; j < arm.patterns.size(); ++j) {
                            if (j > 0) result += " || ";
                            if (is_type_arm) {
                                const auto ident = static_cast<const IdentExpr*>(arm.patterns[j]);
                                result += "std::is_same_v<_T, ";
                                result += map_type(source.slice(ident->name));
                                result += '>';
                            } else {
                                result += "_cv == ";
                                generate_expr(result, *arm.patterns[j], source);
                            }
                        }
                        result += ") {\n";
                    }

                    for (const auto stmt : arm.body->statements) {
                        generate_stmt(result, *stmt, source, 4);
                        result += '\n';
                    }

                    if (!arm.is_wildcard) result += '}';
                }
                for (auto i = 0u; i < else_depth; ++i) result += '}';
            } else {
                // Value-only match
                // Check if all arms are single-expression (for ternary optimization)
                auto all_single_expr = true;
                for (const auto& arm : e.arms) {
                    if (arm.body->statements.size() != 1
                        || arm.body->statements[0]->kind != StmtKind::ExprStmt) {
                        all_single_expr = false;
                        break;
                    }
                }

                if (all_single_expr) {
                    // Ternary chain
                    for (auto i = 0uz; i < e.arms.size(); ++i) {
                        const auto& arm = e.arms[i];
                        if (arm.is_wildcard) {
                            result += " : ";
                        } else {
                            if (i > 0) result += " : ";
                            for (auto j = 0uz; j < arm.patterns.size(); ++j) {
                                if (j > 0) result += " || ";
                                generate_expr(result, *e.value, source);
                                result += " == ";
                                generate_expr(result, *arm.patterns[j], source);
                            }
                            result += " ? ";
                        }
                        const auto es = static_cast<const ExprStmt*>(arm.body->statements[0]);
                        generate_expr(result, *es->expr, source);
                    }
                } else {
                    // Flat if/else chain for multi-statement arms
                    for (auto i = 0uz; i < e.arms.size(); ++i) {
                        const auto& arm = e.arms[i];

                        if (arm.is_wildcard) {
                            result += " else {\n";
                        } else {
                            if (i == 0) result += "if (";
                            else result += "else if (";

                            for (auto j = 0uz; j < arm.patterns.size(); ++j) {
                                if (j > 0) result += " || ";
                                generate_expr(result, *e.value, source);
                                result += " == ";
                                generate_expr(result, *arm.patterns[j], source);
                            }
                            result += ") {\n";
                        }

                        for (const auto stmt : arm.body->statements) {
                            generate_stmt(result, *stmt, source, 4);
                            result += '\n';
                        }
                        result += '}';
                    }
                }
            }

            return;
        }
    }
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

export constexpr auto generate(std::span<const TopLevelItem> items, const SourceFile& source, std::uint8_t standard, bool default_include_std) noexcept -> std::string {
    auto result = std::string();
    result.reserve(source.text().size() * 2);

    if (default_include_std) {
        result += generate_include_preamble(standard);
        result += '\n';
    }

    for (const auto& item : items) {
        std::visit(Overloaded {
            [&](const ImportItem&   it) noexcept { generate_import  (result, it, source); },
            [&](const EnumItem&     it) noexcept { generate_enum    (result, it, source); },
            [&](const StructItem&   it) noexcept { generate_struct  (result, it, source); },
            [&](const FunctionItem& it) noexcept { generate_function(result, it, source); },
        }, item);
        result += "\n\n";
    }

    return result;
}
