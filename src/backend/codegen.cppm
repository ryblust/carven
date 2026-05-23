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

constexpr auto generate_import(const ImportItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::string("");
    const auto name = text_at(source, item.module_name);

    if (!item.is_std_module) {
        std::format_to(std::back_inserter(result), "import {};\n", name);
    }
    if (item.using_wildcard) {
        std::format_to(std::back_inserter(result), "using namespace {};\n", name);
    } else {
        for (const auto decl : item.using_decls) {
            std::format_to(std::back_inserter(result), "using {}::{};\n", name, text_at(source, decl));
        }
    }

    return result;
}

constexpr auto generate_enum(const EnumItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::format("enum class {}{} {{\n",
        text_at(source, item.name),
        is_empty(item.size) ? "" : std::format(" : {}", map_type(text_at(source, item.size)))
    );

    for (const auto field : item.fields) {
        std::format_to(std::back_inserter(result), "    {},\n", text_at(source, field));
    }

    return result += "};";
}

constexpr auto generate_struct(const StructItem& item, std::string_view source) noexcept -> std::string {
    auto result = std::format("struct {} {{\n", text_at(source, item.name));

    for (const auto field : item.fields) {
        std::format_to(std::back_inserter(result),
            "    {} {};\n", map_type(text_at(source, field.type)), text_at(source, field.name)
        );
    }

    return result += "};";
}

constexpr auto generate_stmt(const Stmt& stmt, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;
constexpr auto generate_expr(const Expr& expr, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;

constexpr auto generate_var_decl(const VarDecl& s, std::string_view source, std::string_view padding = "") noexcept -> std::string {
    const auto keyword = text_at(source, s.keyword);
    auto result = std::format("{}{}{}",
        padding,
        keyword == "let" ? "const auto " : keyword == "const" ? "constexpr auto " : "auto ",
        text_at(source, s.name)
    );

    if (const auto has_type = !is_empty(s.type); s.init != nullptr) {
        result += has_type
            ? std::format(" = {}({})", map_type(text_at(source, s.type)), generate_expr(*s.init, source))
            : std::format(" = {}", generate_expr(*s.init, source));
    } else if (has_type) {
        std::format_to(std::back_inserter(result), " = {}()", map_type(text_at(source, s.type)));
    }

    return result;
}

constexpr auto generate_function(const FunctionItem& item, std::string_view source) noexcept -> std::string {
    const auto name = text_at(source, item.name);
    auto params = std::string();

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto p = item.params[i];
        if (i > 0) params += ", ";

        params += is_empty(p.type)
            ? std::format("auto {}", text_at(source, p.name))
            : std::format("{} {}", map_type(text_at(source, p.type)), text_at(source, p.name));
    }

    const auto return_type = [&] noexcept -> std::string {
        if (name == "main") return " -> int";
        if (!is_empty(item.return_type)) {
            return std::format(" -> {}", map_type(text_at(source, item.return_type)));
        } else {
            return "";
        }
    }();

    auto result = std::format("auto {}({}) noexcept{} {{\n", name, params, return_type);

    walk_stmts(item.body->statements, [&](const Stmt& stmt) noexcept {
        result += generate_stmt(stmt, source, 4);
        result += '\n';
    });

    return result += '}';
}

constexpr auto generate_stmt(const Stmt& stmt, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    const auto padding = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const BlockStmt& s) noexcept -> std::string {
            auto result = padding + "{\n";

            walk_stmts(s.statements, [&](const Stmt& stmt) noexcept {
                result += generate_stmt(stmt, source, indent + 4);
                result += '\n';
            });

            return result + padding + '}';
        },
        [&](const ExprStmt& s) noexcept -> std::string {
            return std::format("{};", generate_expr(*s.expr, source, indent));
        },
        [&](const EmptyStmt&) noexcept -> std::string {
            return padding + ";";
        },
        [&](const VarDecl& s) noexcept -> std::string {
            return std::format("{};", generate_var_decl(s, source, padding));
        },
        [&](const ReturnStmt& s) noexcept -> std::string {
            return s.value != nullptr
                ? std::format("{}return {};", padding, generate_expr(*s.value, source))
                : std::format("{}return;", padding);
        },
        [&](const WhileStmt& s) noexcept -> std::string {
            auto result = std::format("{}while ({})", padding, generate_expr(*s.condition, source));

            if (std::get_if<BlockStmt>(s.body)) {
                result += " {\n";
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    result += generate_stmt(body_stmt, source, indent + 4);
                    result += '\n';
                });
                std::format_to(std::back_inserter(result), "{}}}", padding);
            } else {
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    std::format_to(std::back_inserter(result), "\n{}",
                        generate_stmt(body_stmt, source, indent + 4));
                });
            }

            return result;
        },
        [&](const ForStmt& s) noexcept -> std::string {
            auto init = std::string();

            if (s.init != nullptr) {
                walk_for_init(
                    *s.init,
                    [&](const VarDecl& d) noexcept {
                        init = generate_var_decl(d, source);
                    },
                    [&](const ExprStmt& es) noexcept {
                        init = generate_expr(*es.expr, source);
                    }
                );
            }

            const auto condition = s.condition != nullptr ? generate_expr(*s.condition, source) : "";
            const auto step = s.step != nullptr ? generate_expr(*s.step, source) : "";
            auto result = std::format("{}for ({}; {}; {})", padding, init, condition, step);

            if (std::get_if<BlockStmt>(s.body)) {
                result += " {\n";
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    result += generate_stmt(body_stmt, source, indent + 4);
                    result += '\n';
                });
                std::format_to(std::back_inserter(result), "{}}}", padding);
            } else {
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    std::format_to(std::back_inserter(result), "\n{}",
                        generate_stmt(body_stmt, source, indent + 4));
                });
            }

            return result;
        }
    }, stmt);
}

constexpr auto generate_expr(const Expr& expr, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    const auto padding = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const LiteralExpr& e) noexcept {
            return padding + text_at(source, e.token);
        },
        [&](const IdentExpr& e) noexcept {
            return padding + text_at(source, e.name);
        },
        [&](const PrefixExpr& e) noexcept {
            return std::format("{}{}{}", padding, text_at(source, e.span), generate_expr(*e.rhs, source));
        },
        [&](const PostfixExpr& e) noexcept {
            return std::format("{}{}{}", padding, generate_expr(*e.lhs, source), text_at(source, e.span));
        },
        [&](const BinaryExpr& e) noexcept {
            return std::format("{}{} {} {}", padding, generate_expr(*e.lhs, source), text_at(source, e.span), generate_expr(*e.rhs, source));
        },
        [&](const CallExpr& e) noexcept {
            auto args = std::string();

            for (auto i = 0uz; i < e.args.size(); ++i) {
                if (i > 0) args += ", ";
                args += generate_expr(*e.args[i], source);
            }

            return std::format("{}{}({})", padding, generate_expr(*e.callee, source), args);
        },
        [&](const IndexExpr& e) noexcept {
            return std::format("{}{}[{}]", padding, generate_expr(*e.lhs, source), generate_expr(*e.index, source));
        },
        [&](const FieldExpr& e) noexcept {
            return std::format("{}{}{}{}", padding, generate_expr(*e.lhs, source), text_at(source, e.dot), text_at(source, e.field));
        },
        [&](const TernaryExpr& e) noexcept {
            return std::format("{}{} ? {} : {}", padding, generate_expr(*e.condition, source), generate_expr(*e.then_branch, source), generate_expr(*e.else_branch, source));
        },
        [&](const GroupExpr& e) noexcept {
            return std::format("{}({})", padding, generate_expr(*e.inner, source));
        },
        [&](const AssignExpr& e) noexcept {
            return std::format("{}{} = {}", padding, generate_expr(*e.lhs, source), generate_expr(*e.rhs, source));
        },
        [&](const CompoundAssignExpr& e) noexcept {
            return std::format("{}{} {} {}", padding, generate_expr(*e.lhs, source), text_at(source, e.span), generate_expr(*e.rhs, source));
        },
        [&](const CommaExpr& e) noexcept {
            return std::format("{}{}, {}", padding, generate_expr(*e.lhs, source), generate_expr(*e.rhs, source));
        },
        [&](const IfExpr& e) noexcept {
            const auto then_expr = e.then_branch.statements.size() == 1
                ? std::get_if<ExprStmt>(e.then_branch.statements[0])
                : nullptr;

            const auto else_expr = e.else_branch.has_value() && e.else_branch->statements.size() == 1
                ? std::get_if<ExprStmt>(e.else_branch->statements[0])
                : nullptr;

            if (then_expr != nullptr && else_expr != nullptr) {
                return std::format(
                    "{}{} ? {} : {}", padding, generate_expr(*e.condition, source), generate_expr(*then_expr->expr, source), generate_expr(*else_expr->expr, source)
                );
            }

            auto result = std::format("{}if ({}) {{\n", padding, generate_expr(*e.condition, source));

            walk_stmts(e.then_branch.statements, [&](const Stmt& stmt) noexcept {
                result += generate_stmt(stmt, source, indent + 4);
                result += '\n';
            });

            std::format_to(std::back_inserter(result), "{}}}", padding);

            if (e.else_branch.has_value()) {
                result += " else {\n";

                walk_stmts(e.else_branch->statements, [&](const Stmt& stmt) noexcept {
                    result += generate_stmt(stmt, source, indent + 4);
                    result += '\n';
                });

                std::format_to(std::back_inserter(result), "{}}}", padding);
            }

            return result;
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

export constexpr auto generate(std::span<const TopLevelItem> items, std::string_view source, std::uint8_t standard, bool default_include_std) noexcept -> std::string {
    auto result = std::string();

    if (default_include_std) {
        result += generate_include_preamble(standard);
        result += '\n';
    }

    for (const auto& item : items) {
        std::visit(Overloaded {
            [&](const ImportItem&   it) noexcept { result += generate_import  (it, source); },
            [&](const EnumItem&     it) noexcept { result += generate_enum    (it, source); },
            [&](const StructItem&   it) noexcept { result += generate_struct  (it, source); },
            [&](const FunctionItem& it) noexcept { result += generate_function(it, source); },
        }, item);
        result += "\n\n";
    }

    return result;
}