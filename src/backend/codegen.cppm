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

constexpr auto generate_import(const ImportItem& item, const SourceFile& source) noexcept -> std::string {
    auto result = std::string("");
    const auto name = source.slice(item.module_name);

    if (!item.is_std_module) {
        std::format_to(std::back_inserter(result), "import {};\n", name);
    }
    if (item.using_wildcard) {
        std::format_to(std::back_inserter(result), "using namespace {};\n", name);
    } else {
        for (const auto decl : item.using_decls) {
            std::format_to(std::back_inserter(result), "using {}::{};\n", name, source.slice(decl));
        }
    }

    return result;
}

constexpr auto generate_enum(const EnumItem& item, const SourceFile& source) noexcept -> std::string {
    auto result = std::format("enum class {}{} {{\n",
        source.slice(item.name),
        item.size.empty() ? "" : std::format(" : {}", map_type(source.slice(item.size)))
    );

    for (const auto field : item.fields) {
        std::format_to(std::back_inserter(result), "    {},\n", source.slice(field));
    }

    return result += "};";
}

constexpr auto generate_struct(const StructItem& item, const SourceFile& source) noexcept -> std::string {
    auto result = std::format("struct {} {{\n", source.slice(item.name));

    for (const auto field : item.fields) {
        std::format_to(
            std::back_inserter(result),
            "    {} {};\n",
            map_type(source.slice(field.type)), source.slice(field.name)
        );
    }

    return result += "};";
}

constexpr auto generate_stmt(const Stmt& stmt, const SourceFile& source, std::uint32_t indent = 0) noexcept -> std::string;
constexpr auto generate_expr(const Expr& expr, const SourceFile& source) noexcept -> std::string;

constexpr auto generate_var_decl(const VarDecl& s, const SourceFile& source, std::string_view padding = "") noexcept -> std::string {
    const auto keyword = source.slice(s.keyword);
    auto result = std::format("{}{}{}",
        padding,
        keyword == "let" ? "const auto " : keyword == "const" ? "constexpr auto " : "auto ",
        source.slice(s.name)
    );

    if (const auto has_type = !s.type.empty(); s.init != nullptr) {
        result += has_type
            ? std::format(" = {}({})", map_type(source.slice(s.type)), generate_expr(*s.init, source))
            : std::format(" = {}", generate_expr(*s.init, source));
    } else if (has_type) {
        std::format_to(std::back_inserter(result), " = {}()", map_type(source.slice(s.type)));
    }

    return result;
}

constexpr auto generate_function(const FunctionItem& item, const SourceFile& source) noexcept -> std::string {
    const auto name = source.slice(item.name);
    auto params = std::string();

    for (auto i = 0uz; i < item.params.size(); ++i) {
        const auto p = item.params[i];
        if (i > 0) params += ", ";

        params += p.type.empty()
            ? std::format("auto {}", source.slice(p.name))
            : std::format("{} {}", map_type(source.slice(p.type)), source.slice(p.name));
    }

    const auto return_type = [&] noexcept -> std::string {
        if (name == "main") return " -> int";
        if (!item.return_type.empty()) {
            return std::format(" -> {}", map_type(source.slice(item.return_type)));
        } else {
            return "";
        }
    }();

    auto result = std::format("auto {}({}) noexcept{} {{\n", name, params, return_type);

    for (const auto* stmt : item.body->statements) {
        result += generate_stmt(*stmt, source, 4);
        result += '\n';
    }

    return result += '}';
}

constexpr auto generate_stmt(const Stmt& stmt, const SourceFile& source, std::uint32_t indent) noexcept -> std::string {
    const auto padding = std::string(indent, ' ');

    switch (stmt.kind) {
        case StmtKind::Block: {
            const auto& s = *static_cast<const BlockStmt*>(&stmt);
            auto result = padding + "{\n";
            for (const auto* body_stmt : s.statements) {
                result += generate_stmt(*body_stmt, source, indent + 4);
                result += '\n';
            }
            return result + padding + '}';
        }
        case StmtKind::ExprStmt: {
            const auto& s = *static_cast<const ExprStmt*>(&stmt);
            return std::format("{}{};", padding, generate_expr(*s.expr, source));
        }
        case StmtKind::Empty:
            return padding + ";";
        case StmtKind::VarDecl: {
            const auto& s = *static_cast<const VarDecl*>(&stmt);
            return std::format("{};", generate_var_decl(s, source, padding));
        }
        case StmtKind::Return: {
            const auto& s = *static_cast<const ReturnStmt*>(&stmt);
            return s.value != nullptr
                ? std::format("{}return {};", padding, generate_expr(*s.value, source))
                : std::format("{}return;", padding);
        }
        case StmtKind::While: {
            const auto& s = *static_cast<const WhileStmt*>(&stmt);
            auto result = std::format("{}while ({})", padding, generate_expr(*s.condition, source));
            if (s.body->kind == StmtKind::Block) {
                result += " {\n";
                for (const auto* body_stmt : static_cast<const BlockStmt*>(s.body)->statements) {
                    result += generate_stmt(*body_stmt, source, indent + 4);
                    result += '\n';
                }
                std::format_to(std::back_inserter(result), "{}}}", padding);
            } else {
                std::format_to(std::back_inserter(result), "\n{}",
                    generate_stmt(*s.body, source, indent + 4));
            }
            return result;
        }
        case StmtKind::For: {
            const auto& s = *static_cast<const ForStmt*>(&stmt);
            auto init = std::string();
            std::visit(Overloaded {
                [&](VarDecl* d) noexcept { if (d) init = generate_var_decl(*d, source); },
                [&](ExprStmt* es) noexcept { if (es) init = generate_expr(*es->expr, source); }
            }, s.init);
            const auto condition = s.condition != nullptr ? generate_expr(*s.condition, source) : "";
            const auto step = s.step != nullptr ? generate_expr(*s.step, source) : "";
            auto result = std::format("{}for ({}; {}; {})", padding, init, condition, step);
            if (s.body->kind == StmtKind::Block) {
                result += " {\n";
                for (const auto* body_stmt : static_cast<const BlockStmt*>(s.body)->statements) {
                    result += generate_stmt(*body_stmt, source, indent + 4);
                    result += '\n';
                }
                std::format_to(std::back_inserter(result), "{}}}", padding);
            } else {
                std::format_to(std::back_inserter(result), "\n{}",
                    generate_stmt(*s.body, source, indent + 4));
            }
            return result;
        }
    }
}

constexpr auto generate_expr(const Expr& expr, const SourceFile& source) noexcept -> std::string {
    switch (expr.kind) {
        case ExprKind::Literal: {
            const auto& e = *static_cast<const LiteralExpr*>(&expr);
            return std::format("{}", source.slice(e.token));
        }
        case ExprKind::Ident: {
            const auto& e = *static_cast<const IdentExpr*>(&expr);
            return std::format("{}", source.slice(e.name));
        }
        case ExprKind::Prefix: {
            const auto& e = *static_cast<const PrefixExpr*>(&expr);
            return std::format("{}{}", source.slice(e.span), generate_expr(*e.rhs, source));
        }
        case ExprKind::Postfix: {
            const auto& e = *static_cast<const PostfixExpr*>(&expr);
            return std::format("{}{}", generate_expr(*e.lhs, source), source.slice(e.span));
        }
        case ExprKind::Binary: {
            const auto& e = *static_cast<const BinaryExpr*>(&expr);
            return std::format("{} {} {}", generate_expr(*e.lhs, source), source.slice(e.span), generate_expr(*e.rhs, source));
        }
        case ExprKind::Call: {
            const auto& e = *static_cast<const CallExpr*>(&expr);
            auto args = std::string();
            for (auto i = 0uz; i < e.args.size(); ++i) {
                if (i > 0) args += ", ";
                args += generate_expr(*e.args[i], source);
            }
            return std::format("{}({})", generate_expr(*e.callee, source), args);
        }
        case ExprKind::Index: {
            const auto& e = *static_cast<const IndexExpr*>(&expr);
            return std::format("{}[{}]", generate_expr(*e.lhs, source), generate_expr(*e.index, source));
        }
        case ExprKind::Field: {
            const auto& e = *static_cast<const FieldExpr*>(&expr);
            return std::format("{}{}{}", generate_expr(*e.lhs, source), source.slice(e.dot), source.slice(e.field));
        }
        case ExprKind::Group: {
            const auto& e = *static_cast<const GroupExpr*>(&expr);
            return std::format("({})", generate_expr(*e.inner, source));
        }
        case ExprKind::Assign: {
            const auto& e = *static_cast<const AssignExpr*>(&expr);
            return std::format("{} = {}", generate_expr(*e.lhs, source), generate_expr(*e.rhs, source));
        }
        case ExprKind::CompoundAssign: {
            const auto& e = *static_cast<const CompoundAssignExpr*>(&expr);
            return std::format("{} {} {}", generate_expr(*e.lhs, source), source.slice(e.span), generate_expr(*e.rhs, source));
        }
        case ExprKind::Comma: {
            const auto& e = *static_cast<const CommaExpr*>(&expr);
            return std::format("{}, {}", generate_expr(*e.lhs, source), generate_expr(*e.rhs, source));
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
                return std::format("{} ? {} : {}",
                    generate_expr(*e.condition, source),
                    generate_expr(*then_expr->expr, source),
                    generate_expr(*else_expr->expr, source));
            }

            auto result = std::format("if ({}) {{\n", generate_expr(*e.condition, source));
            for (const auto* stmt : e.then_branch->statements) {
                result += generate_stmt(*stmt, source, 4);
                result += '\n';
            }
            result += '}';

            if (e.else_branch) {
                result += " else {\n";
                for (const auto* stmt : e.else_branch->statements) {
                    result += generate_stmt(*stmt, source, 4);
                    result += '\n';
                }
                result += '}';
            }

            return result;
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
