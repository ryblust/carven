export module zero.driver.dump;

import zero.common.filesystem;
import zero.common.source;
import zero.driver.pipeline;
import zero.frontend.token;
import zero.frontend.lexer;
import zero.frontend.ast;
import zero.frontend.parser;
import std;

auto dump_indent(std::string& out, int level) noexcept -> void {
    for (auto i = 0; i < level; ++i) out.append("  ");
}

auto dump_expr(const Expr& expr, std::string_view source, std::string& out, int level) noexcept -> void;
auto dump_stmt(const Stmt& stmt, std::string_view source, std::string& out, int level) noexcept -> void;

auto dump_top_level(const TopLevelItem& item, std::string_view source, std::string& out, int level) noexcept -> void {
    std::visit(Overloaded {
        [&](const ImportItem& it) -> void {
            dump_indent(out, level);
            out.append("Import: ").append(text_at(source, it.module_name));
            if (!it.using_decls.empty()) {
                out.append(" { ");
                for (auto i = 0uz; i < it.using_decls.size(); ++i) {
                    if (i > 0) out.append(", ");
                    out.append(text_at(source, it.using_decls[i]));
                }
                out.append(" }");
            }
            out.push_back('\n');
        },
        [&](const EnumItem& it) -> void {
            dump_indent(out, level);
            out.append("Enum: ").append(text_at(source, it.name));
            if (!is_empty(it.size)) out.append(": ").append(text_at(source, it.size));
            out.append(" { ");
            for (auto i = 0uz; i < it.fields.size(); ++i) {
                if (i > 0) out.append(", ");
                out.append(text_at(source, it.fields[i]));
            }
            out.append(" }\n");
        },
        [&](const StructItem& it) -> void {
            dump_indent(out, level);
            out.append("Struct: ").append(text_at(source, it.name)).append(" {\n");
            for (const auto& field : it.fields) {
                dump_indent(out, level + 1);
                out.append(text_at(source, field.name)).append(": ").append(text_at(source, field.type)).append("\n");
            }
            dump_indent(out, level);
            out.append("}\n");
        },
        [&](const FunctionItem& it) -> void {
            dump_indent(out, level);
            out.append("fn ").append(text_at(source, it.name)).append("(");
            for (auto i = 0uz; i < it.params.size(); ++i) {
                if (i > 0) out.append(", ");
                out.append(text_at(source, it.params[i].name)).append(": ").append(text_at(source, it.params[i].type));
            }
            out.append(")");
            if (!is_empty(it.return_type)) out.append(" -> ").append(text_at(source, it.return_type));
            out.append(" {\n");
            walk_stmts(it.body->statements, [&](const Stmt& stmt) { dump_stmt(stmt, source, out, level + 1); });
            dump_indent(out, level);
            out.append("}\n");
        }
    }, item);
}

auto dump_expr(const Expr& expr, std::string_view source, std::string& out, int level) noexcept -> void {
    std::visit(Overloaded {
        [&](const LiteralExpr& e) -> void {
            out.append("Literal(").append(text_at(source, e.token)).append(")");
        },
        [&](const IdentExpr& e) -> void {
            out.append("Ident(").append(text_at(source, e.name)).append(")");
        },
        [&](const PrefixExpr& e) -> void {
            out.append("Prefix(").append(to_string(e.op)).append(" ");
            dump_expr(*e.rhs, source, out, level);
            out.append(")");
        },
        [&](const PostfixExpr& e) -> void {
            out.append("Postfix(");
            dump_expr(*e.lhs, source, out, level);
            out.append(" ").append(to_string(e.op)).append(")");
        },
        [&](const BinaryExpr& e) -> void {
            out.append("Binary(");
            dump_expr(*e.lhs, source, out, level);
            out.append(" ").append(to_string(e.op)).append(" ");
            dump_expr(*e.rhs, source, out, level);
            out.append(")");
        },
        [&](const CallExpr& e) -> void {
            out.append("Call(");
            dump_expr(*e.callee, source, out, level);
            for (const auto* arg : e.args) {
                out.append(" ");
                dump_expr(*arg, source, out, level);
            }
            out.append(")");
        },
        [&](const IndexExpr& e) -> void {
            out.append("Index(");
            dump_expr(*e.lhs, source, out, level);
            out.append("[");
            dump_expr(*e.index, source, out, level);
            out.append("])");
        },
        [&](const FieldExpr& e) -> void {
            out.append("Field(");
            dump_expr(*e.lhs, source, out, level);
            out.append(text_at(source, e.dot)).append(text_at(source, e.field)).append(")");
        },
        [&](const TernaryExpr& e) -> void {
            out.append("Ternary(");
            dump_expr(*e.condition, source, out, level);
            out.append(" ? ");
            dump_expr(*e.then_branch, source, out, level);
            out.append(" : ");
            dump_expr(*e.else_branch, source, out, level);
            out.append(")");
        },
        [&](const GroupExpr& e) -> void {
            out.append("Group(");
            dump_expr(*e.inner, source, out, level);
            out.append(")");
        },
        [&](const AssignExpr& e) -> void {
            out.append("Assign(");
            dump_expr(*e.lhs, source, out, level);
            out.append(" = ");
            dump_expr(*e.rhs, source, out, level);
            out.append(")");
        },
        [&](const CompoundAssignExpr& e) -> void {
            out.append("CompoundAssign(");
            dump_expr(*e.lhs, source, out, level);
            out.append(" ").append(to_string(e.op)).append("= ");
            dump_expr(*e.rhs, source, out, level);
            out.append(")");
        },
        [&](const CommaExpr& e) -> void {
            out.append("Comma(");
            dump_expr(*e.lhs, source, out, level);
            out.append(", ");
            dump_expr(*e.rhs, source, out, level);
            out.append(")");
        },
        [&](const IfExpr& e) -> void {
            out.append("If(");
            dump_expr(*e.condition, source, out, level);
            out.append(") {\n");
            walk_stmts(e.then_branch.statements, [&](const Stmt& stmt) { dump_stmt(stmt, source, out, level + 1); });
            dump_indent(out, level);
            out.append("}");
            if (e.else_branch.has_value()) {
                out.append(" else {\n");
                walk_stmts(e.else_branch->statements, [&](const Stmt& stmt) { dump_stmt(stmt, source, out, level + 1); });
                dump_indent(out, level);
                out.append("}");
            }
        }
    }, expr);
}

auto dump_stmt(const Stmt& stmt, std::string_view source, std::string& out, int level) noexcept -> void {
    std::visit(Overloaded {
        [&](const BlockStmt& s) -> void {
            dump_indent(out, level);
            out.append("Block {\n");
            walk_stmts(s.statements, [&](const Stmt& stmt) { dump_stmt(stmt, source, out, level + 1); });
            dump_indent(out, level);
            out.append("}\n");
        },
        [&](const ExprStmt& s) -> void {
            dump_indent(out, level);
            dump_expr(*s.expr, source, out, level);
            out.append(";\n");
        },
        [&](const EmptyStmt&) -> void {
            dump_indent(out, level);
            out.append(";\n");
        },
        [&](const VarDecl& s) -> void {
            dump_indent(out, level);
            out.append(text_at(source, s.keyword)).append(" ").append(text_at(source, s.name));
            if (!is_empty(s.type)) out.append(": ").append(text_at(source, s.type));
            if (s.init != nullptr) {
                out.append(" = ");
                dump_expr(*s.init, source, out, level);
            }
            out.append(";\n");
        },
        [&](const ReturnStmt& s) -> void {
            dump_indent(out, level);
            out.append("return");
            if (s.value != nullptr) {
                out.append(" ");
                dump_expr(*s.value, source, out, level);
            }
            out.append(";\n");
        },
        [&](const WhileStmt& s) -> void {
            dump_indent(out, level);
            out.append("while (");
            dump_expr(*s.condition, source, out, level);
            out.append(") ");
            walk_body(s.body, [&](const Stmt& body_stmt) {
                if (std::get_if<BlockStmt>(s.body)) {
                    out.append("{\n");
                    dump_stmt(body_stmt, source, out, level + 1);
                    dump_indent(out, level);
                    out.append("}\n");
                } else {
                    out.append("\n");
                    dump_stmt(body_stmt, source, out, level + 1);
                }
            });
        },
        [&](const ForStmt& s) -> void {
            dump_indent(out, level);
            out.append("for (");
            if (s.init != nullptr) {
                walk_for_init(*s.init, [&](const VarDecl& d) {
                    out.append(text_at(source, d.keyword)).append(" ").append(text_at(source, d.name));
                    if (!is_empty(d.type)) out.append(": ").append(text_at(source, d.type));
                    if (d.init != nullptr) {
                        out.append(" = ");
                        dump_expr(*d.init, source, out, level);
                    }
                }, [&](const ExprStmt& es) { dump_expr(*es.expr, source, out, level); });
            }
            out.append("; ");
            if (s.condition != nullptr) dump_expr(*s.condition, source, out, level);
            out.append("; ");
            if (s.step != nullptr) dump_expr(*s.step, source, out, level);
            out.append(") ");
            walk_body(s.body, [&](const Stmt& body_stmt) {
                if (std::get_if<BlockStmt>(s.body)) {
                    out.append("{\n");
                    dump_stmt(body_stmt, source, out, level + 1);
                    dump_indent(out, level);
                    out.append("}\n");
                } else {
                    out.append("\n");
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
                pos.line, pos.column, display_token_type(token.kind), text_at(source, token.span));
        }
        if (!driver.only_tokens) std::println();
    }

    if (!driver.only_tokens) {
        const auto result = parse(tokens, source);
        if (result.has_errors()) {
            for (const auto& error : result.errors) std::println("{}", format_parse_error(error));
            return 1;
        }
        std::print("{}", dump_ast(result, source));
    }

    return 0;
}
