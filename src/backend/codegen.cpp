module zero.backend.codegen;

import zero.common.source;
import zero.frontend.parser.ast;
import std;

namespace {

auto map_type(std::string_view zero_type) noexcept -> std::string_view {
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

auto generate_import(const ImportItem& item, std::string_view source) noexcept -> std::string {
    const auto mod = text_at(source, item.module_name);
    auto result = std::string("import ").append(mod).append(";\n");

    for (const auto& decl : item.using_decls) {
        result.append("using ")
              .append(mod)
              .append("::").append(text_at(source, decl)).append(";\n");
    }

    return result;
}

auto generate_enum(const EnumItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("enum class ").append(text_at(source, item.name)).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(text_at(source, field)).append(",\n");
    }

    return result.append("};\n");
}

auto generate_struct(const StructItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("struct ").append(text_at(source, item.name)).append(" {\n");

    for (const auto& field : item.fields) {
        result.append("    ").append(map_type(text_at(source, field.type)));
        result.push_back(' ');
        result.append(text_at(source, field.name)).append(";\n");
    }

    return result.append("};\n");
}

auto generate_stmt(const Stmt& stmt, std::string_view source) noexcept -> std::string;
auto generate_expr(const Expr& expr, std::string_view source) noexcept -> std::string;

auto generate_function(const FunctionItem& item, std::string_view source) noexcept -> std::string {
    const auto name = text_at(source, item.name);
    auto result = std::string("auto ").append(name).append("(");

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto& p = item.params[i];
        result.append(map_type(text_at(source, p.type)));
        result.push_back(' ');
        result.append(text_at(source, p.name));

        if (i + 1 < item.params.size()) result.append(", ");
    }

    result.append(") noexcept");

    if (name == "main") {
        result.append(" -> int");
    } else if (is_empty(item.return_type)) {
        result.append(" -> void");
    } else {
        result.append(" -> ").append(map_type(text_at(source, item.return_type)));
    }

    result.append(" {\n");

    for (const auto* stmt : item.body->statements) {
        result.append(generate_stmt(*stmt, source));
    }

    return result.append("}\n");
}

auto generate_stmt(const Stmt& stmt, std::string_view source) noexcept -> std::string {
    return std::visit(Overloaded {
        [&](const BlockStmt& s) -> std::string {
            auto r = std::string("{\n");
            for (const auto* st : s.statements) {
                r.append(generate_stmt(*st, source));
            }
            return r.append("}\n");
        },
        [&](const ExprStmt& s) -> std::string {
            return generate_expr(*s.expr, source).append(";\n");
        },
        [&](const EmptyStmt&) -> std::string {
            return ";\n";
        },
        [&](const VarDecl& s) -> std::string {
            const auto kw = text_at(source, s.keyword);
            auto r = std::string();

            if (kw == "let") {
                r.append("const auto ");
            } else if (kw == "const") {
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
                return std::string("return ").append(generate_expr(*s.value, source)).append(";\n");
            }
            return "return;\n";
        },
        [&](const WhileStmt& s) -> std::string {
            auto r = std::string("while (").append(generate_expr(*s.condition, source)).append(") ");
            r.append(generate_stmt(*s.body, source));
            return r;
        },
        [&](const ForStmt& s) -> std::string {
            auto r = std::string("for (");
            if (s.init != nullptr) {
                std::visit(Overloaded {
                    [&](const VarDecl& d) {
                        r.append(generate_stmt(VarDecl{d}, source));
                        if (r.ends_with(";\n")) r.resize(r.size() - 2);
                    },
                    [&](const ExprStmt& es) {
                        auto e = generate_expr(*es.expr, source);
                        r.append(e);
                    }
                }, *s.init);
            }
            r.append("; ");
            if (s.condition != nullptr) r.append(generate_expr(*s.condition, source));
            r.append("; ");
            if (s.step != nullptr) r.append(generate_expr(*s.step, source));
            r.append(") ").append(generate_stmt(*s.body, source));
            return r;
        }
    }, stmt);
}

auto generate_expr(const Expr& expr, std::string_view source) noexcept -> std::string {
    return std::visit(Overloaded {
        [&](const LiteralExpr& e) -> std::string {
            return std::string(text_at(source, e.token));
        },
        [&](const IdentExpr& e) -> std::string {
            return std::string(text_at(source, e.name));
        },
        [&](const PrefixExpr& e) -> std::string {
            return std::string(text_at(source, e.span)).append(generate_expr(*e.rhs, source));
        },
        [&](const PostfixExpr& e) -> std::string {
            return generate_expr(*e.lhs, source).append(text_at(source, e.span));
        },
        [&](const BinaryExpr& e) -> std::string {
            return generate_expr(*e.lhs, source)
                .append(" ").append(text_at(source, e.span)).append(" ")
                .append(generate_expr(*e.rhs, source));
        },
        [&](const CallExpr& e) -> std::string {
            auto r = generate_expr(*e.callee, source).append("(");
            for (auto i = 0uz; i < e.args.size(); ++i) {
                if (i > 0) r.append(", ");
                r.append(generate_expr(*e.args[i], source));
            }
            return r.append(")");
        },
        [&](const IndexExpr& e) -> std::string {
            return generate_expr(*e.lhs, source)
                .append("[").append(generate_expr(*e.index, source)).append("]");
        },
        [&](const FieldExpr& e) -> std::string {
            return generate_expr(*e.lhs, source)
                .append(text_at(source, e.dot)).append(text_at(source, e.field));
        },
        [&](const TernaryExpr& e) -> std::string {
            return generate_expr(*e.condition, source).append(" ? ")
                .append(generate_expr(*e.then_branch, source)).append(" : ")
                .append(generate_expr(*e.else_branch, source));
        },
        [&](const GroupExpr& e) -> std::string {
            return std::string("(").append(generate_expr(*e.inner, source)).append(")");
        },
        [&](const AssignExpr& e) -> std::string {
            return generate_expr(*e.lhs, source).append(" = ").append(generate_expr(*e.rhs, source));
        },
        [&](const CompoundAssignExpr& e) -> std::string {
            return generate_expr(*e.lhs, source)
                .append(" ").append(text_at(source, e.span)).append(" ")
                .append(generate_expr(*e.rhs, source));
        },
        [&](const CommaExpr& e) -> std::string {
            return generate_expr(*e.lhs, source).append(", ").append(generate_expr(*e.rhs, source));
        },
        [&](const IfExpr& e) -> std::string {
            const auto then_expr = e.then_branch.statements.size() == 1
                ? std::get_if<ExprStmt>(e.then_branch.statements[0]) : nullptr;
            const auto else_expr = e.else_branch.has_value() && e.else_branch->statements.size() == 1
                ? std::get_if<ExprStmt>(e.else_branch->statements[0]) : nullptr;

            if (then_expr != nullptr && else_expr != nullptr) {
                auto r = generate_expr(*e.condition, source).append(" ? ");
                r.append(generate_expr(*then_expr->expr, source)).append(" : ");
                return r.append(generate_expr(*else_expr->expr, source));
            }

            auto r = std::string("if (").append(generate_expr(*e.condition, source)).append(") ");
            r.append(generate_stmt(Stmt(e.then_branch), source));
            if (e.else_branch.has_value()) {
                r.append("else ").append(generate_stmt(Stmt(*e.else_branch), source));
            }
            return r;
        }
    }, expr);
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

auto generate_include_preamble(std::uint8_t standard) noexcept -> std::string {
    auto result = std::string(CPP_HEADERS_BASE);

    if (standard >= 17) result.append(CPP_HEADERS_17);
    if (standard >= 20) result.append(CPP_HEADERS_20);
    if (standard >= 23) result.append(CPP_HEADERS_23);
    if (standard >= 26) result.append(CPP_HEADERS_26);

    return result.append("\n");
}

} // namespace

auto generate(std::span<const TopLevelItem> items, std::string_view source, std::uint8_t standard, bool default_include_std) noexcept -> std::string {
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
