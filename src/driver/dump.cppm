export module carven.driver.dump;

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.report;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

auto dump(const TopLevelItem& item, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;
auto dump(const Expr& expr, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;
auto dump(const Stmt& stmt, std::string_view source, std::uint32_t indent = 0) noexcept -> std::string;
auto dump(const ParseResult& parse_result, std::string_view source) noexcept -> std::string;

auto dump(const Type* type, std::string_view source) noexcept -> std::string {
    if (type == nullptr) return {};
    switch (type->kind) {
        case TypeKind::Name:
            return std::string(slice(source, static_cast<const NameType*>(type)->span));
        case TypeKind::Array: {
            const auto arr = static_cast<const ArrayType*>(type);
            return std::format("[{}; {}]", slice(source, arr->elem_type), slice(source, arr->size));
        }
    }
    return {};
}

auto dump(const TopLevelItem& item, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    const auto padding = std::string(indent, ' ');

    return std::visit(Overloaded {
        [&](const ImportItem& it) noexcept -> std::string {
            auto result = std::format("{}Import: {}", padding, slice(source, it.module_name));

            if (!it.using_decls.empty()) {
                result += " { ";

                for (auto i = 0uz; i < it.using_decls.size(); ++i) {
                    if (i > 0) result += ", ";
                    result += slice(source, it.using_decls[i]);
                }

                result += " }";
            }

            return result += '\n';
        },
        [&](const EnumItem& it) noexcept -> std::string {
            auto result = std::format("{}Enum: {}{} {{ ",
                padding,
                slice(source, it.name),
                it.size != nullptr ? std::format(": {}", dump(it.size, source)) : ""
            );

            for (auto i = 0uz; i < it.fields.size(); ++i) {
                if (i > 0) result += ", ";
                result += slice(source, it.fields[i]);
            }

            return result += " }\n";
        },
        [&](const StructItem& it) noexcept -> std::string {
            auto result = std::format("{}Struct: {} {{\n", padding, slice(source, it.name));
            const auto field_padding = std::string(indent + 2, ' ');

            for (const auto& field : it.fields) {
                std::format_to(
                    std::back_inserter(result),
                    "{}{}: {}\n",
                    field_padding, slice(source, field.name), dump(field.type, source)
                );
            }

            return std::format("{}{}}}\n", result, padding);
        },
        [&](const FunctionItem& it) noexcept -> std::string {
            auto result = std::format("{}fn {}(", padding, slice(source, it.name));

            for (auto i = 0uz; i < it.params.size(); ++i) {
                if (i > 0) result += ", ";
                std::format_to(
                    std::back_inserter(result),
                    "{}: {}",
                    slice(source, it.params[i].name), dump(it.params[i].type, source)
                );
            }

            std::format_to(
                std::back_inserter(result),
                "){} {{\n",
                it.return_type != nullptr ? std::format(" -> {}", dump(it.return_type, source)) : ""
            );

            for (const auto stmt : it.body->statements) {
                result += dump(*stmt, source, indent + 2);
            }

            return std::format("{}{}}}\n", result, padding);
        }
    }, item);
}

auto dump(const Expr& expr, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    switch (expr.kind) {
        case ExprKind::Literal: {
            return std::format("Literal({})", slice(source, static_cast<const LiteralExpr*>(&expr)->token));
        }
        case ExprKind::Ident: {
            return std::format("Ident({})", slice(source, static_cast<const IdentExpr*>(&expr)->name));
        }
        case ExprKind::Prefix: {
            const auto e = static_cast<const PrefixExpr*>(&expr);
            return std::format("Prefix({} {})", e->op, dump(*e->rhs, source));
        }
        case ExprKind::Postfix: {
            const auto e = static_cast<const PostfixExpr*>(&expr);
            return std::format("Postfix({} {})", dump(*e->lhs, source), e->op);
        }
        case ExprKind::Binary: {
            const auto e = static_cast<const BinaryExpr*>(&expr);
            return std::format("Binary({} {} {})", dump(*e->lhs, source), e->op, dump(*e->rhs, source));
        }
        case ExprKind::Call: {
            const auto e = static_cast<const CallExpr*>(&expr);
            auto result = std::format("Call({}", dump(*e->callee, source));
            for (const auto arg : e->args) {
                result += ' ';
                result += dump(*arg, source);
            }
            return result += ')';
        }
        case ExprKind::Index: {
            const auto e = static_cast<const IndexExpr*>(&expr);
            return std::format("Index({}[{}])", dump(*e->lhs, source), dump(*e->index, source));
        }
        case ExprKind::Field: {
            const auto e = static_cast<const FieldExpr*>(&expr);
            return std::format("Field({}{}{})", dump(*e->lhs, source), slice(source, e->dot), slice(source, e->field));
        }
        case ExprKind::Group: {
            return std::format("Group({})", dump(*static_cast<const GroupExpr*>(&expr)->inner, source));
        }
        case ExprKind::Assign: {
            const auto e = static_cast<const AssignExpr*>(&expr);
            return std::format("Assign({} = {})", dump(*e->lhs, source), dump(*e->rhs, source));
        }
        case ExprKind::CompoundAssign: {
            const auto e = static_cast<const CompoundAssignExpr*>(&expr);
            return std::format("CompoundAssign({} {}={})", dump(*e->lhs, source), e->op, dump(*e->rhs, source));
        }
        case ExprKind::Comma: {
            const auto e = static_cast<const CommaExpr*>(&expr);
            return std::format("Comma({}, {})", dump(*e->lhs, source), dump(*e->rhs, source));
        }
        case ExprKind::If: {
            const auto e = static_cast<const IfExpr*>(&expr);
            const auto padding = std::string(indent, ' ');
            auto result = std::format("If({}) {{\n", dump(*e->condition, source));

            for (const auto stmt : e->then_branch->statements) {
                result += dump(*stmt, source, indent + 2);
            }

            std::format_to(std::back_inserter(result), "{}}}", padding);
            if (e->else_branch) {
                result += " else {\n";
                for (const auto stmt : e->else_branch->statements) {
                    result += dump(*stmt, source, indent + 2);
                }
                std::format_to(std::back_inserter(result), "{}}}", padding);
            }

            return result;
        }
        case ExprKind::Array: {
            const auto e = static_cast<const ArrayExpr*>(&expr);
            auto result = std::string("[");
            for (auto i = 0uz; i < e->elements.size(); ++i) {
                if (i > 0) result += ", ";
                result += dump(*e->elements[i], source);
            }
            result += ']';
            return result;
        }
        case ExprKind::Match: {
            const auto e = static_cast<const MatchExpr*>(&expr);
            const auto padding = std::string(indent, ' ');
            auto result = std::format("match {} {{\n", dump(*e->value, source));
            for (const auto& arm : e->arms) {
                result += padding + "  ";
                if (arm.is_wildcard) {
                    result += "_ => {\n";
                } else {
                    for (auto j = 0uz; j < arm.patterns.size(); ++j) {
                        if (j > 0) result += " | ";
                        result += dump(*arm.patterns[j], source);
                    }
                    result += " => {\n";
                }
                for (const auto stmt : arm.body->statements) {
                    result += dump(*stmt, source, indent + 4);
                }
                result += padding + "  }\n";
            }
            std::format_to(std::back_inserter(result), "{}}}", padding);
            return result;
        }
    }
}

auto dump(const Stmt& stmt, std::string_view source, std::uint32_t indent) noexcept -> std::string {
    const auto padding = std::string(indent, ' ');

    switch (stmt.kind) {
        case StmtKind::Block: {
            auto result = std::format("{}Block {{\n", padding);
            for (const auto body_stmt : static_cast<const BlockStmt*>(&stmt)->statements) {
                result += dump(*body_stmt, source, indent + 2);
            }
            return std::format("{}{}}}\n", result, padding);
        }
        case StmtKind::ExprStmt: {
            return std::format("{}{};\n", padding, dump(*static_cast<const ExprStmt*>(&stmt)->expr, source));
        }
        case StmtKind::Empty: return padding + ";\n";
        case StmtKind::VarDecl: {
            const auto s = static_cast<const VarDecl*>(&stmt);
            auto result = std::format("{}{} {}{}",
                padding,
                slice(source, s->keyword),
                slice(source, s->name),
                s->type != nullptr ? std::format(": {}", dump(s->type, source)) : ""
            );
            if (s->init != nullptr) {
                result += " = ";
                result += dump(*s->init, source);
            }
            return result += ";\n";
        }
        case StmtKind::Return: {
            const auto s = static_cast<const ReturnStmt*>(&stmt);
            if (s->value != nullptr) {
                return std::format("{}return {};\n", padding, dump(*s->value, source));
            }
            return padding + "return;\n";
        }
        case StmtKind::While: {
            const auto s = static_cast<const WhileStmt*>(&stmt);
            auto result = std::format("{}while ({})", padding, dump(*s->condition, source, indent));
            if (s->body->kind == StmtKind::Block) {
                result += " {\n";
                for (const auto body_stmt : static_cast<const BlockStmt*>(s->body)->statements) {
                    result += dump(*body_stmt, source, indent + 2);
                }
                std::format_to(std::back_inserter(result), "{}}}\n", padding);
            } else {
                result += '\n';
                result += dump(*s->body, source, indent + 2);
            }
            return result;
        }
        case StmtKind::For: {
            const auto s = static_cast<const ForStmt*>(&stmt);
            auto init_str = std::string();
            std::visit(Overloaded {
                [&](VarDecl* d) noexcept {
                    if (d) {
                        init_str = std::format("{} {}{}",
                            slice(source, d->keyword),
                            slice(source, d->name),
                            d->type != nullptr ? std::format(": {}", dump(d->type, source)) : ""
                        );
                        if (d->init != nullptr) {
                            init_str += " = ";
                            init_str += dump(*d->init, source);
                        }
                    }
                },
                [&](ExprStmt* es) noexcept {
                    if (es) init_str = dump(*es->expr, source);
                }
            }, s->init);
            const auto condition = s->condition != nullptr ? dump(*s->condition, source) : std::string();
            const auto step = s->step != nullptr ? dump(*s->step, source) : std::string();
            auto result = std::format("{}for ({}; {}; {})", padding, init_str, condition, step);
            if (s->body->kind == StmtKind::Block) {
                result += " {\n";
                for (const auto body_stmt : static_cast<const BlockStmt*>(s->body)->statements) {
                    result += dump(*body_stmt, source, indent + 2);
                }
                std::format_to(std::back_inserter(result), "{}}}\n", padding);
            } else {
                result += '\n';
                result += dump(*s->body, source, indent + 2);
            }
            return result;
        }
    }
}

auto dump(const ParseResult& parse_result, std::string_view source) noexcept -> std::string {
    auto result = std::string();
    result.reserve(source.size() * 4);

    for (const auto& item : parse_result.items) {
        result += dump(item, source, 0);
    }

    return result;
}

export auto dump(const Driver& driver) noexcept -> int {
    if (driver.input_files.empty()) {
        std::println("carven dump: error: no input file");
        return 1;
    }

    auto src = SourceFile::from_file(driver.input_files[0]);
    if (!src) {
        std::println("carven dump: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }

    const auto text = src->text();
    const auto tokens = tokenize(text);

    if (!driver.only_ast) {
        const auto line_offsets = LineOffsets(text);
        for (const auto token : tokens) {
            const auto pos = line_offsets.location(token.span.start);
            std::println("{:>4}:{:<2}    {:<20}  {}",
                pos.line, pos.column, std::format("{}", token.kind), slice(text, token.span)
            );
        }
        if (!driver.only_tokens) std::println();
    }

    if (!driver.only_tokens) {
        const auto parse_result = parse(tokens, text);
        if (!parse_result.errors.empty()) {
            report_errors(parse_result.errors, text, driver.input_files[0]);
            return 1;
        }
        std::print("{}", dump(parse_result, text));
    }

    return 0;
}
