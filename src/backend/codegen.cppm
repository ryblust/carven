export module zero.backend.codegen;

import zero.common.source;
import zero.frontend.parser.ast;
import zero.frontend.ast_walker;
import std;

constexpr auto map_type(std::string_view zero_type) noexcept -> std::string_view {
    if (zero_type == "i8")    return "std::int8_t";
    if (zero_type == "i16")   return "std::int16_t";
    if (zero_type == "i32")   return "std::int32_t";
    if (zero_type == "i64")   return "std::int64_t";
    if (zero_type == "u8")    return "std::uint8_t";
    if (zero_type == "u16")   return "std::uint16_t";
    if (zero_type == "u32")   return "std::uint32_t";
    if (zero_type == "u64")   return "std::uint64_t";
    if (zero_type == "bool")  return "bool";
    if (zero_type == "f32")   return "float";
    if (zero_type == "f64")   return "double";
    if (zero_type == "char")  return "char";
    if (zero_type == "usize") return "std::size_t";

    return zero_type;
}

constexpr auto generate_import(const ImportItem& item, std::string_view source) noexcept -> std::string {
    const auto mod = text_at(source, item.module_name);
    auto result = std::string();

    if (!item.is_std_module) {
        result.append("import ").append(mod).append(";\n");
    }

    if (item.using_wildcard) {
        result.append("using namespace ").append(mod).append(";\n");
    } else {
        for (const auto& decl : item.using_decls) {
            result.append("using ")
                  .append(mod)
                  .append("::").append(text_at(source, decl)).append(";\n");
        }
    }

    return result;
}

constexpr auto generate_enum(const EnumItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("enum class ").append(text_at(source, item.name));

    if (!is_empty(item.size)) {
        result.append(" : ").append(map_type(text_at(source, item.size)));
    }

    result.append(" {\n");

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

constexpr auto generate_stmt(const Stmt& stmt, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;
constexpr auto generate_expr(const Expr& expr, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;

constexpr auto generate_function(const FunctionItem& item, std::string_view source) noexcept -> std::string {
    const auto name = text_at(source, item.name);
    auto result = std::string("auto ").append(name).append("(");

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto& p = item.params[i];
        if (is_empty(p.type)) {
            result.append("auto ");
        } else {
            result.append(map_type(text_at(source, p.type)));
            result.push_back(' ');
        }
        result.append(text_at(source, p.name));

        if (i + 1 < item.params.size()) result.append(", ");
    }

    result.append(") noexcept");

    if (name == "main") {
        result.append(" -> int");
    } else if (!is_empty(item.return_type)) {
        result.append(" -> ").append(map_type(text_at(source, item.return_type)));
    }

    result.append(" {\n");

    walk_stmts(item.body->statements, [&](const Stmt& stmt) {
        result.append(generate_stmt(stmt, source, 4));
    });

    return result.append("}\n");
}

constexpr auto generate_stmt(const Stmt& stmt, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    const auto pad = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const BlockStmt& s) -> std::string {
            auto r = pad + "{\n";
            walk_stmts(s.statements, [&](const Stmt& stmt) {
                r.append(generate_stmt(stmt, source, indent + 4));
            });
            return r + pad + "}\n";
        },
        [&](const ExprStmt& s) -> std::string {
            return generate_expr(*s.expr, source, indent) + ";\n";
        },
        [&](const EmptyStmt&) -> std::string {
            return pad + ";\n";
        },
        [&](const VarDecl& s) -> std::string {
            const auto keyword_text = text_at(source, s.keyword);
            auto r = pad;

            if (keyword_text == "let") {
                r.append("const auto ");
            } else if (keyword_text == "const") {
                r.append("constexpr auto ");
            } else {
                r.append("auto ");
            }

            r.append(text_at(source, s.name));

            const auto has_type = !is_empty(s.type);
            if (s.init != nullptr) {
                r.append(" = ");
                if (has_type) {
                    r.append(map_type(text_at(source, s.type))).append("(");
                    r.append(generate_expr(*s.init, source));
                    r.append(")");
                } else {
                    r.append(generate_expr(*s.init, source));
                }
            } else if (has_type) {
                r.append(" = ").append(map_type(text_at(source, s.type))).append("()");
            }

            return r.append(";\n");
        },
        [&](const ReturnStmt& s) -> std::string {
            if (s.value != nullptr) {
                return pad + "return " + generate_expr(*s.value, source) + ";\n";
            }
            return pad + "return;\n";
        },
        [&](const WhileStmt& s) -> std::string {
            auto r = pad + "while (" + generate_expr(*s.condition, source, 0) + ") ";
            walk_body(s.body, [&](const Stmt& body_stmt) {
                if (std::get_if<BlockStmt>(s.body)) {
                    r += "{\n";
                    r += generate_stmt(body_stmt, source, indent + 4);
                    r += pad + "}\n";
                } else {
                    r += "\n" + generate_stmt(body_stmt, source, indent + 4);
                }
            });
            return r;
        },
        [&](const ForStmt& s) -> std::string {
            auto r = pad + "for (";
            if (s.init != nullptr) {
                walk_for_init(*s.init, [&](const VarDecl& d) {
                    const auto keyword_text = text_at(source, d.keyword);
                    if (keyword_text == "let") {
                        r.append("const auto ");
                    } else if (keyword_text == "const") {
                        r.append("constexpr auto ");
                    } else {
                        r.append("auto ");
                    }
                    r.append(text_at(source, d.name));
                    const auto has_type = !is_empty(d.type);
                    if (d.init != nullptr) {
                        r.append(" = ");
                        if (has_type) {
                            r.append(map_type(text_at(source, d.type))).append("(");
                            r.append(generate_expr(*d.init, source));
                            r.append(")");
                        } else {
                            r.append(generate_expr(*d.init, source));
                        }
                    } else if (has_type) {
                        r.append(" = ").append(map_type(text_at(source, d.type))).append("()");
                    }
                }, [&](const ExprStmt& es) {
                    r.append(generate_expr(*es.expr, source));
                });
            }
            r.append("; ");
            if (s.condition != nullptr) r.append(generate_expr(*s.condition, source));
            r.append("; ");
            if (s.step != nullptr) r.append(generate_expr(*s.step, source));
            r += ") ";
            walk_body(s.body, [&](const Stmt& body_stmt) {
                if (std::get_if<BlockStmt>(s.body)) {
                    r += "{\n";
                    r += generate_stmt(body_stmt, source, indent + 4);
                    r += pad + "}\n";
                } else {
                    r += "\n" + generate_stmt(body_stmt, source, indent + 4);
                }
            });
            return r;
        }
    }, stmt);
}

constexpr auto generate_expr(const Expr& expr, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    const auto pad = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const LiteralExpr& e) noexcept {
            return pad + text_at(source, e.token);
        },
        [&](const IdentExpr& e) noexcept {
            return pad + text_at(source, e.name);
        },
        [&](const PrefixExpr& e) noexcept {
            return pad + text_at(source, e.span) + generate_expr(*e.rhs, source, 0);
        },
        [&](const PostfixExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0) + text_at(source, e.span);
        },
        [&](const BinaryExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0)
                + " " + text_at(source, e.span) + " "
                + generate_expr(*e.rhs, source, 0);
        },
        [&](const CallExpr& e) noexcept {
            auto r = pad + generate_expr(*e.callee, source, 0) + "(";
            for (auto i = 0uz; i < e.args.size(); ++i) {
                if (i > 0) r += ", ";
                r += generate_expr(*e.args[i], source, 0);
            }
            return r + ")";
        },
        [&](const IndexExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0)
                + "[" + generate_expr(*e.index, source, 0) + "]";
        },
        [&](const FieldExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0)
                + text_at(source, e.dot) + text_at(source, e.field);
        },
        [&](const TernaryExpr& e) noexcept {
            return pad + generate_expr(*e.condition, source, 0) + " ? "
                + generate_expr(*e.then_branch, source, 0) + " : "
                + generate_expr(*e.else_branch, source, 0);
        },
        [&](const GroupExpr& e) noexcept {
            return pad + "(" + generate_expr(*e.inner, source, 0) + ")";
        },
        [&](const AssignExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0) + " = " + generate_expr(*e.rhs, source, 0);
        },
        [&](const CompoundAssignExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0)
                + " " + text_at(source, e.span) + " "
                + generate_expr(*e.rhs, source, 0);
        },
        [&](const CommaExpr& e) noexcept {
            return pad + generate_expr(*e.lhs, source, 0) + ", " + generate_expr(*e.rhs, source, 0);
        },
        [&](const IfExpr& e) noexcept {
            const auto then_expr = e.then_branch.statements.size() == 1
                ? std::get_if<ExprStmt>(e.then_branch.statements[0]) : nullptr;
            const auto else_expr = e.else_branch.has_value() && e.else_branch->statements.size() == 1
                ? std::get_if<ExprStmt>(e.else_branch->statements[0]) : nullptr;

            if (then_expr != nullptr && else_expr != nullptr) {
                return pad + generate_expr(*e.condition, source, 0) + " ? "
                    + generate_expr(*then_expr->expr, source, 0) + " : "
                    + generate_expr(*else_expr->expr, source, 0);
            }

            auto r = pad + "if (" + generate_expr(*e.condition, source, 0) + ") ";
            r += "{\n";
            walk_stmts(e.then_branch.statements, [&](const Stmt& stmt) {
                r += generate_stmt(stmt, source, indent + 4);
            });
            r += pad + "}";
            if (e.else_branch.has_value()) {
                r += " else {\n";
                walk_stmts(e.else_branch->statements, [&](const Stmt& stmt) {
                    r += generate_stmt(stmt, source, indent + 4);
                });
                r += pad + "}";
            }
            r += "\n";
            return r;
        }
    }, expr);
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

export constexpr auto generate(
    std::span<const TopLevelItem> items, std::string_view source, std::uint8_t standard, bool default_include_std
) noexcept -> std::string {
    auto result = std::string();

    if (default_include_std) {
        result.append(generate_include_preamble(standard));
    }

    for (const auto& item : items) {
        std::visit(Overloaded {
            [&](const ImportItem&   it) { result.append(generate_import  (it, source)); },
            [&](const EnumItem&     it) { result.append(generate_enum    (it, source)); },
            [&](const StructItem&   it) { result.append(generate_struct  (it, source)); },
            [&](const FunctionItem& it) { result.append(generate_function(it, source)); },
        }, item);
        result.push_back('\n');
    }

    return result;
}
