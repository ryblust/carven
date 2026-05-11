module zero.driver.handler;

import zero.common.filesystem;
import zero.common.source;
import zero.driver.pipeline;
import zero.frontend.lexer.token;
import zero.frontend.lexer;
import zero.frontend.parser.ast;
import zero.frontend.parser;

import std;

namespace {

auto dump_indent(std::string& out, int level) noexcept -> void {
    for (auto i = 0; i < level; ++i) out.append("  ");
}

auto dump_expr(const Expr& expr, std::string_view source, std::string& out, int level) noexcept -> void;
auto dump_stmt(const Stmt& stmt, std::string_view source, std::string& out, int level) noexcept -> void;

auto dump_item(const ImportItem& item, std::string_view source, std::string& out, int level) noexcept -> void {
    dump_indent(out, level);
    out.append("Import: ").append(text_at(source, item.module_name));
    if (!item.using_decls.empty()) {
        out.append(" { ");
        for (auto i = 0uz; i < item.using_decls.size(); ++i) {
            if (i > 0) out.append(", ");
            out.append(text_at(source, item.using_decls[i]));
        }
        out.append(" }");
    }
    out.push_back('\n');
}

auto dump_item(const EnumItem& item, std::string_view source, std::string& out, int level) noexcept -> void {
    dump_indent(out, level);
    out.append("Enum: ").append(text_at(source, item.name));
    if (!is_empty(item.size)) out.append(": ").append(text_at(source, item.size));
    out.append(" { ");
    for (auto i = 0uz; i < item.fields.size(); ++i) {
        if (i > 0) out.append(", ");
        out.append(text_at(source, item.fields[i]));
    }
    out.append(" }\n");
}

auto dump_item(const StructItem& item, std::string_view source, std::string& out, int level) noexcept -> void {
    dump_indent(out, level);
    out.append("Struct: ").append(text_at(source, item.name)).append(" {\n");
    for (const auto& field : item.fields) {
        dump_indent(out, level + 1);
        out.append(text_at(source, field.name)).append(": ").append(text_at(source, field.type)).append("\n");
    }
    dump_indent(out, level);
    out.append("}\n");
}

auto dump_item(const FunctionItem& item, std::string_view source, std::string& out, int level) noexcept -> void {
    dump_indent(out, level);
    out.append("fn ").append(text_at(source, item.name)).append("(");
    for (auto i = 0uz; i < item.params.size(); ++i) {
        if (i > 0) out.append(", ");
        out.append(text_at(source, item.params[i].name)).append(": ").append(text_at(source, item.params[i].type));
    }
    out.append(")");
    if (!is_empty(item.return_type)) out.append(" -> ").append(text_at(source, item.return_type));
    out.append(" {\n");
    for (const auto* stmt : item.body->statements) dump_stmt(*stmt, source, out, level + 1);
    dump_indent(out, level);
    out.append("}\n");
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
            for (const auto* stmt : e.then_branch.statements) dump_stmt(*stmt, source, out, level + 1);
            dump_indent(out, level);
            out.append("}");
            if (e.else_branch.has_value()) {
                out.append(" else {\n");
                for (const auto* stmt : e.else_branch->statements) dump_stmt(*stmt, source, out, level + 1);
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
            for (const auto* st : s.statements) dump_stmt(*st, source, out, level + 1);
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
            if (const auto body = std::get_if<BlockStmt>(s.body)) {
                out.append("{\n");
                for (const auto* st : body->statements) dump_stmt(*st, source, out, level + 1);
                dump_indent(out, level);
                out.append("}\n");
            } else {
                out.append("\n");
                dump_stmt(*s.body, source, out, level + 1);
            }
        },
        [&](const ForStmt& s) -> void {
            dump_indent(out, level);
            out.append("for (");
            if (s.init != nullptr) {
                std::visit(Overloaded {
                    [&](const VarDecl& d) -> void {
                        out.append(text_at(source, d.keyword)).append(" ").append(text_at(source, d.name));
                        if (!is_empty(d.type)) out.append(": ").append(text_at(source, d.type));
                        if (d.init != nullptr) {
                            out.append(" = ");
                            dump_expr(*d.init, source, out, level);
                        }
                    },
                    [&](const ExprStmt& es) -> void { dump_expr(*es.expr, source, out, level); }
                }, *s.init);
            }
            out.append("; ");
            if (s.condition != nullptr) dump_expr(*s.condition, source, out, level);
            out.append("; ");
            if (s.step != nullptr) dump_expr(*s.step, source, out, level);
            out.append(") ");
            if (const auto body = std::get_if<BlockStmt>(s.body)) {
                out.append("{\n");
                for (const auto* st : body->statements) dump_stmt(*st, source, out, level + 1);
                dump_indent(out, level);
                out.append("}\n");
            } else {
                out.append("\n");
                dump_stmt(*s.body, source, out, level + 1);
            }
        }
    }, stmt);
}

auto dump_ast(const ParseResult& result, std::string_view source) noexcept -> std::string {
    auto out = std::string();
    out.reserve(source.size() * 4);

    for (const auto& item : result.items) {
        std::visit([&](const auto& it) -> void { dump_item(it, source, out, 0); }, item);
    }

    return out;
}

} // namespace

auto run(const Driver& driver) -> int {
    if (const auto content = read_file(driver.input_files[0]); content) {
        return driver.run_single_file({
            .filename = std::string(driver.input_files[0]), .content = std::move(*content)
        });
    } else {
        std::println("zero run: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }
}

auto dump(const Driver& driver) -> int {
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
        if (!driver.only_tokens) std::println("");
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

auto build([[maybe_unused]] const Driver& driver) -> int {
    std::println("zero build: not yet implemented");
    return 0;
}

auto check([[maybe_unused]] const Driver& driver) -> int {
    std::println("zero check: not yet implemented");
    return 0;
}
