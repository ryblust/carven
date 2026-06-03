export module carven.driver.dump;

import carven.common.source;
import carven.driver.pipeline;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

auto dump(const TopLevelItem& item, const SourceFile& source, std::uint32_t indent = 0) noexcept -> std::string;
auto dump(const Expr& expr, const SourceFile& source, std::uint32_t indent = 0) noexcept -> std::string;
auto dump(const Stmt& stmt, const SourceFile& source, std::uint32_t indent = 0) noexcept -> std::string;

auto dump(const TopLevelItem& item, const SourceFile& source, std::uint32_t indent) noexcept -> std::string {
    const auto pad = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const ImportItem& it) noexcept -> std::string {
            auto result = std::format("{}Import: {}", pad, source.slice(it.module_name));

            if (!it.using_decls.empty()) {
                result += " { ";

                for (auto i = 0uz; i < it.using_decls.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += source.slice(it.using_decls[i]);
                }

                result += " }";
            }

            return result += '\n';
        },
        [&](const EnumItem& it) noexcept -> std::string {
            auto result = std::format("{}Enum: {}{} {{ ",
                pad,
                source.slice(it.name),
                it.size.empty() ? "" : std::format(": {}", source.slice(it.size))
            );

            for (auto i = 0uz; i < it.fields.size(); ++i) {
                if (i > 0) result += ", ";
                result += source.slice(it.fields[i]);
            }

            return result += " }\n";
        },
        [&](const StructItem& it) noexcept -> std::string {
            auto result = std::format("{}Struct: {} {{\n", pad, source.slice(it.name));
            const auto field_pad = std::string(indent + 2, ' ');

            for (const auto& field : it.fields) {
                std::format_to(
                    std::back_inserter(result),
                    "{}{}: {}\n",
                    field_pad, source.slice(field.name), source.slice(field.type)
                );
            }

            return std::format("{}{}}}\n", result, pad);
        },
        [&](const FunctionItem& it) noexcept -> std::string {
            auto result = std::format("{}fn {}(", pad, source.slice(it.name));

            for (auto i = 0uz; i < it.params.size(); ++i) {
                if (i > 0) result += ", ";
                std::format_to(
                    std::back_inserter(result),
                    "{}: {}",
                    source.slice(it.params[i].name), source.slice(it.params[i].type)
                );
            }

            std::format_to(
                std::back_inserter(result),
                "){} {{\n",
                it.return_type.empty() ? "" : std::format(" -> {}", source.slice(it.return_type))
            );

            walk_stmts(it.body->statements, [&](const Stmt& stmt) noexcept {
                result += dump(stmt, source, indent + 2);
            });

            return std::format("{}{}}}\n", result, pad);
        }
    }, item);
}


auto dump(const Expr& expr, const SourceFile& source, std::uint32_t indent) noexcept -> std::string {
    return std::visit(Overloaded {
        [&](const LiteralExpr& e) noexcept {
            return std::format("Literal({})", source.slice(e.token));
        },
        [&](const IdentExpr& e) noexcept {
            return std::format("Ident({})", source.slice(e.name));
        },
        [&](const PrefixExpr& e) noexcept {
            return std::format("Prefix({} {})", e.op, dump(*e.rhs, source));
        },
        [&](const PostfixExpr& e) noexcept {
            return std::format("Postfix({} {})", dump(*e.lhs, source), e.op);
        },
        [&](const BinaryExpr& e) noexcept {
            return std::format("Binary({} {} {})", dump(*e.lhs, source), e.op, dump(*e.rhs, source));
        },
        [&](const CallExpr& e) noexcept -> std::string {
            auto result = std::format("Call({}", dump(*e.callee, source));
            for (const auto* arg : e.args) {
                result += ' ';
                result += dump(*arg, source);
            }
            return result += ')';
        },
        [&](const IndexExpr& e) noexcept {
            return std::format("Index({}[{}])", dump(*e.lhs, source), dump(*e.index, source));
        },
        [&](const FieldExpr& e) noexcept {
            return std::format("Field({}{}{})", dump(*e.lhs, source), source.slice(e.dot), source.slice(e.field));
        },
        [&](const TernaryExpr& e) noexcept {
            return std::format("Ternary({} ? {} : {})",
                dump(*e.condition, source), dump(*e.then_branch, source), dump(*e.else_branch, source));
        },
        [&](const GroupExpr& e) noexcept {
            return std::format("Group({})", dump(*e.inner, source));
        },
        [&](const AssignExpr& e) noexcept {
            return std::format("Assign({} = {})", dump(*e.lhs, source), dump(*e.rhs, source));
        },
        [&](const CompoundAssignExpr& e) noexcept {
            return std::format("CompoundAssign({} {}={})", dump(*e.lhs, source), e.op, dump(*e.rhs, source));
        },
        [&](const CommaExpr& e) noexcept {
            return std::format("Comma({}, {})", dump(*e.lhs, source), dump(*e.rhs, source));
        },
        [&](const IfExpr& e) noexcept -> std::string {
            const auto pad = std::string(indent, ' ');
            auto result = std::format("If({}) {{\n", dump(*e.condition, source));

            walk_stmts(e.then_branch.statements, [&](const Stmt& stmt) noexcept {
                result += dump(stmt, source, indent + 2);
            });

            std::format_to(std::back_inserter(result), "{}}}", pad);
            if (e.else_branch.has_value()) {
                result += " else {\n";
                walk_stmts(e.else_branch->statements, [&](const Stmt& stmt) noexcept {
                    result += dump(stmt, source, indent + 2);
                });
                std::format_to(std::back_inserter(result), "{}}}", pad);
            }

            return result;
        }
    }, expr);
}

auto dump(const Stmt& stmt, const SourceFile& source, std::uint32_t indent) noexcept -> std::string {
    const auto pad = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const BlockStmt& s) noexcept -> std::string {
            auto result = std::format("{}Block {{\n", pad);

            walk_stmts(s.statements, [&](const Stmt& stmt) noexcept {
                result += dump(stmt, source, indent + 2);
            });

            return std::format("{}{}}}\n", result, pad);
        },
        [&](const ExprStmt& s) noexcept {
            return std::format("{}{};\n", pad, dump(*s.expr, source, indent));
        },
        [&](const EmptyStmt&) noexcept {
            return pad + ";\n";
        },
        [&](const VarDecl& s) noexcept -> std::string {
            auto result = std::format("{}{} {}{}",
                pad,
                source.slice(s.keyword),
                source.slice(s.name),
                s.type.empty() ? "" : std::format(": {}", source.slice(s.type))
            );

            if (s.init != nullptr) {
                result += " = ";
                result += dump(*s.init, source);
            }

            return result += ";\n";
        },
        [&](const ReturnStmt& s) noexcept {
            if (s.value != nullptr) {
                return std::format("{}return {};\n", pad, dump(*s.value, source));
            }

            return pad + "return;\n";
        },
        [&](const WhileStmt& s) noexcept -> std::string {
            auto result = std::format("{}while ({})", pad, dump(*s.condition, source, indent));

            if (std::get_if<BlockStmt>(s.body)) {
                result += " {\n";
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    result += dump(body_stmt, source, indent + 2);
                });
                std::format_to(std::back_inserter(result), "{}}}\n", pad);
            } else {
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    result += '\n';
                    result += dump(body_stmt, source, indent + 2);
                });
            }

            return result;
        },
        [&](const ForStmt& s) noexcept -> std::string {
            auto init_str = std::string();

            if (s.init != nullptr) {
                walk_for_init(*s.init,
                    [&](const VarDecl& d) noexcept {
                        init_str = std::format("{} {}{}",
                            source.slice(d.keyword),
                            source.slice(d.name),
                            d.type.empty() ? "" : std::format(": {}", source.slice(d.type))
                        );
                        if (d.init != nullptr) {
                            init_str += " = ";
                            init_str += dump(*d.init, source);
                        }
                    },
                    [&](const ExprStmt& es) noexcept {
                        init_str = dump(*es.expr, source);
                    }
                );
            }

            const auto condition = s.condition != nullptr ? dump(*s.condition, source) : std::string();
            const auto step = s.step != nullptr ? dump(*s.step, source) : std::string();
            auto result = std::format("{}for ({}; {}; {})", pad, init_str, condition, step);

            if (std::get_if<BlockStmt>(s.body)) {
                result += " {\n";
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    result += dump(body_stmt, source, indent + 2);
                });
                std::format_to(std::back_inserter(result), "{}}}\n", pad);
            } else {
                walk_body(s.body, [&](const Stmt& body_stmt) noexcept {
                    result += '\n';
                    result += dump(body_stmt, source, indent + 2);
                });
            }

            return result;
        }
    }, stmt);
}


auto dump_ast(const ParseResult& parse_result, const SourceFile& source) noexcept -> std::string {
    auto result = std::string();
    result.reserve(source.text().size() * 4);

    for (const auto& item : parse_result.items) {
        result += dump(item, source, 0);
    }

    return result;
}

export auto dump(const Driver& driver) noexcept -> int {
    auto source = SourceFile::from_file(driver.input_files[0]);
    if (!source) {
        std::println("carven dump: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }

    const auto tokens = tokenize(source->text());

    if (!driver.only_ast) {
        for (const auto token : tokens) {
            const auto pos = source->location(token.span.start);
            std::println("{:>4}:{:<2}    {:<20}  {}",
                pos.line, pos.column, std::format("{}", token.kind), source->slice(token.span)
            );
        }
        if (!driver.only_tokens) std::println();
    }

    if (!driver.only_tokens) {
        const auto parse_result = parse(tokens, *source);
        if (!parse_result.errors.empty()) {
            std::println("{}", parse_result.errors);
            return 1;
        }
        std::print("{}", dump_ast(parse_result, *source));
    }

    return 0;
}
