export module zero.driver.dump;

import zero.common.filesystem;
import zero.common.source;
import zero.driver.pipeline;
import zero.frontend.token;
import zero.frontend.lexer;
import zero.frontend.ast;
import zero.frontend.parser;
import std;

auto dump_indent(std::string& out, std::size_t level) noexcept -> void {
    for (auto i = 0uz; i < level; ++i) out += "  ";
}

auto dump_expr(const Expr& expr, std::string_view source, std::string& out, std::size_t level) noexcept -> void;
auto dump_stmt(const Stmt& stmt, std::string_view source, std::string& out, std::size_t level) noexcept -> void;

auto dump_top_level(const TopLevelItem& item, std::string_view source, std::string& out, std::size_t level) noexcept -> void {
    std::visit(Overloaded {
        [&](const ImportItem& it) noexcept -> void {
            dump_indent(out, level);
            out += std::format("Import: {}", text_at(source, it.module_name));
            if (!it.using_decls.empty()) {
                out += " { ";
                for (auto i = 0uz; i < it.using_decls.size(); ++i) {
                    if (i > 0) out += ", ";
                    out += text_at(source, it.using_decls[i]);
                }
                out += " }";
            }
            out += '\n';
        },
        [&](const EnumItem& it) noexcept -> void {
            dump_indent(out, level);
            out += std::format("Enum: {}", text_at(source, it.name));
            if (!is_empty(it.size)) out += std::format(": {}", text_at(source, it.size));
            out += " { ";
            for (auto i = 0uz; i < it.fields.size(); ++i) {
                if (i > 0) out += ", ";
                out += text_at(source, it.fields[i]);
            }
            out += " }\n";
        },
        [&](const StructItem& it) noexcept -> void {
            dump_indent(out, level);
            out += std::format("Struct: {} {{\n", text_at(source, it.name));
            for (const auto& field : it.fields) {
                dump_indent(out, level + 1);
                out += std::format("{}: {}\n", text_at(source, field.name), text_at(source, field.type));
            }
            dump_indent(out, level);
            out += "}\n";
        },
        [&](const FunctionItem& it) noexcept -> void {
            dump_indent(out, level);
            out += std::format("fn {}(", text_at(source, it.name));
            for (auto i = 0uz; i < it.params.size(); ++i) {
                if (i > 0) out += ", ";
                out += std::format("{}: {}", text_at(source, it.params[i].name), text_at(source, it.params[i].type));
            }
            out += ")";
            if (!is_empty(it.return_type)) out += std::format(" -> {}", text_at(source, it.return_type));
            out += " {\n";
            walk_stmts(it.body->statements, [&](const Stmt& stmt) noexcept { dump_stmt(stmt, source, out, level + 1); });
            dump_indent(out, level);
            out += "}\n";
        }
    }, item);
}

auto dump_expr(const Expr& expr, std::string_view source, std::string& out, std::size_t level) noexcept -> void {
    std::visit(Overloaded {
        [&](const LiteralExpr& e) noexcept -> void {
            out += std::format("Literal({})", text_at(source, e.token));
        },
        [&](const IdentExpr& e) noexcept -> void {
            out += std::format("Ident({})", text_at(source, e.name));
        },
        [&](const PrefixExpr& e) noexcept -> void {
            out += std::format("Prefix({} ", e.op);
            dump_expr(*e.rhs, source, out, level);
            out += ")";
        },
        [&](const PostfixExpr& e) noexcept -> void {
            out += "Postfix(";
            dump_expr(*e.lhs, source, out, level);
            out += std::format(" {})", e.op);
        },
        [&](const BinaryExpr& e) noexcept -> void {
            out += "Binary(";
            dump_expr(*e.lhs, source, out, level);
            out += std::format(" {} ", e.op);
            dump_expr(*e.rhs, source, out, level);
            out += ")";
        },
        [&](const CallExpr& e) noexcept -> void {
            out += "Call(";
            dump_expr(*e.callee, source, out, level);
            for (const auto* arg : e.args) {
                out += " ";
                dump_expr(*arg, source, out, level);
            }
            out += ")";
        },
        [&](const IndexExpr& e) noexcept -> void {
            out += "Index(";
            dump_expr(*e.lhs, source, out, level);
            out += "[";
            dump_expr(*e.index, source, out, level);
            out += "])";
        },
        [&](const FieldExpr& e) noexcept -> void {
            out += "Field(";
            dump_expr(*e.lhs, source, out, level);
            out += std::format("{}{})", text_at(source, e.dot), text_at(source, e.field));
        },
        [&](const TernaryExpr& e) noexcept -> void {
            out += "Ternary(";
            dump_expr(*e.condition, source, out, level);
            out += " ? ";
            dump_expr(*e.then_branch, source, out, level);
            out += " : ";
            dump_expr(*e.else_branch, source, out, level);
            out += ")";
        },
        [&](const GroupExpr& e) noexcept -> void {
            out += "Group(";
            dump_expr(*e.inner, source, out, level);
            out += ")";
        },
        [&](const AssignExpr& e) noexcept -> void {
            out += "Assign(";
            dump_expr(*e.lhs, source, out, level);
            out += " = ";
            dump_expr(*e.rhs, source, out, level);
            out += ")";
        },
        [&](const CompoundAssignExpr& e) noexcept -> void {
            out += "CompoundAssign(";
            dump_expr(*e.lhs, source, out, level);
            out += std::format(" {}=", e.op);
            dump_expr(*e.rhs, source, out, level);
            out += ")";
        },
        [&](const CommaExpr& e) noexcept -> void {
            out += "Comma(";
            dump_expr(*e.lhs, source, out, level);
            out += ", ";
            dump_expr(*e.rhs, source, out, level);
            out += ")";
        },
        [&](const IfExpr& e) noexcept -> void {
            out += "If(";
            dump_expr(*e.condition, source, out, level);
            out += ") {\n";
            walk_stmts(e.then_branch.statements, [&](const Stmt& stmt) noexcept { dump_stmt(stmt, source, out, level + 1); });
            dump_indent(out, level);
            out += "}";
            if (e.else_branch.has_value()) {
                out += " else {\n";
                walk_stmts(e.else_branch->statements, [&](const Stmt& stmt) noexcept { dump_stmt(stmt, source, out, level + 1); });
                dump_indent(out, level);
                out += "}";
            }
        }
    }, expr);
}

auto dump_stmt(const Stmt& stmt, std::string_view source, std::string& out, std::size_t level) noexcept -> void {
    std::visit(Overloaded {
        [&](const BlockStmt& s) noexcept -> void {
            dump_indent(out, level);
            out += "Block {\n";
            walk_stmts(s.statements, [&](const Stmt& stmt) noexcept { dump_stmt(stmt, source, out, level + 1); });
            dump_indent(out, level);
            out += "}\n";
        },
        [&](const ExprStmt& s) noexcept -> void {
            dump_indent(out, level);
            dump_expr(*s.expr, source, out, level);
            out += ";\n";
        },
        [&](const EmptyStmt&) noexcept -> void {
            dump_indent(out, level);
            out += ";\n";
        },
        [&](const VarDecl& s) noexcept -> void {
            dump_indent(out, level);
            out += std::format("{} {}", text_at(source, s.keyword), text_at(source, s.name));
            if (!is_empty(s.type)) out += std::format(": {}", text_at(source, s.type));
            if (s.init != nullptr) {
                out += " = ";
                dump_expr(*s.init, source, out, level);
            }
            out += ";\n";
        },
        [&](const ReturnStmt& s) noexcept -> void {
            dump_indent(out, level);
            out += "return";
            if (s.value != nullptr) {
                out += " ";
                dump_expr(*s.value, source, out, level);
            }
            out += ";\n";
        },
        [&](const WhileStmt& s) noexcept -> void {
            dump_indent(out, level);
            out += "while (";
            dump_expr(*s.condition, source, out, level);
            out += ") ";
            walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                if (std::get_if<BlockStmt>(s.body)) {
                    out += "{\n";
                    dump_stmt(body_stmt, source, out, level + 1);
                    dump_indent(out, level);
                    out += "}\n";
                } else {
                    out += "\n";
                    dump_stmt(body_stmt, source, out, level + 1);
                }
            });
        },
        [&](const ForStmt& s) noexcept -> void {
            dump_indent(out, level);
            out += "for (";
            if (s.init != nullptr) {
                walk_for_init(*s.init, [&](const VarDecl& d) noexcept {
                    out += std::format("{} {}", text_at(source, d.keyword), text_at(source, d.name));
                    if (!is_empty(d.type)) out += std::format(": {}", text_at(source, d.type));
                    if (d.init != nullptr) {
                        out += " = ";
                        dump_expr(*d.init, source, out, level);
                    }
                }, [&](const ExprStmt& es) noexcept { dump_expr(*es.expr, source, out, level); });
            }
            out += "; ";
            if (s.condition != nullptr) dump_expr(*s.condition, source, out, level);
            out += "; ";
            if (s.step != nullptr) dump_expr(*s.step, source, out, level);
            out += ") ";
            walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                if (std::get_if<BlockStmt>(s.body)) {
                    out += "{\n";
                    dump_stmt(body_stmt, source, out, level + 1);
                    dump_indent(out, level);
                    out += "}\n";
                } else {
                    out += "\n";
                    dump_stmt(body_stmt, source, out, level + 1);
                }
            });
        }
    }, stmt);
}

auto dump_ast(const ParseResult& result, std::string_view source) noexcept -> std::string {
    auto out = std::string();
    out.reserve(source.size() * 4);

    for (const auto& item : result.items) {
        dump_top_level(item, source, out, 0);
    }

    return out;
}

export auto dump(const Driver& driver) noexcept -> int {
    const auto content = read_file(driver.input_files[0]);
    if (!content) {
        std::println("zero dump: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }

    const auto source = std::string_view(*content);
    const auto tokens = tokenize(source);

    if (!driver.only_ast) {
        for (const auto token : tokens) {
            const auto pos = location_at(source, token.span.start);
            std::println("{:>4}:{:<2}    {:<20}  {}",
                pos.line, pos.column, std::format("{}", token.kind), text_at(source, token.span)
            );
        }
        if (!driver.only_tokens) std::println();
    }

    if (!driver.only_tokens) {
        const auto result = parse(tokens, source);
        if (!result.errors.empty()) {
            std::println("{}", result.errors);
            return 1;
        }
        std::print("{}", dump_ast(result, source));
    }

    return 0;
}
