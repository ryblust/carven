module carven:frontend.dump.ast.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.dump.ast;
import :frontend.dump.text;
import :frontend.lex.token;
import :source.manager;
import :source.text;
import std;

ASTDumper::ASTDumper(
    ASTView ast,
    std::string_view source_text,
    std::string_view source_origin
) noexcept
    : ast(ast),
      source_text(source_text),
      source_origin(source_origin) {}

auto ASTDumper::source_label(Span span) const noexcept -> std::string {
    return format_source_label(source_text, span);
}

auto ASTDumper::append_line(std::string_view prefix, bool is_last, std::string_view label) noexcept
    -> void {
    append_dump_line(output, prefix, is_last, label);
}

auto ASTDumper::child_prefix(std::string_view prefix, bool is_last) noexcept -> std::string {
    return std::format("{}{}", prefix, is_last ? "   " : "│  ");
}

auto ASTDumper::render_span_field(
    std::string_view prefix,
    bool is_last,
    std::string_view name,
    Span span
) noexcept -> void {
    if (span.empty()) {
        append_line(prefix, is_last, std::format("{} <absent>", name));
    } else {
        append_line(prefix, is_last, std::format("{} {}", name, source_label(span)));
    }
}

auto ASTDumper::render() noexcept -> std::string {
    const auto& ast_module = ast.ast_module();
    output = std::format(
        "SourceModule {} {}\n",
        format_dump_span(ast_module.span),
        quote_dump_text(source_origin)
    );
    render_list(
        {},
        false,
        "imports",
        ast_module.imports,
        [&](ASTImportID declaration, std::string_view prefix, bool is_last) noexcept {
            render_import(declaration, prefix, is_last);
        }
    );
    render_list(
        {},
        true,
        "items",
        ast_module.items,
        [&](ASTItemID item, std::string_view prefix, bool is_last) noexcept {
            render_top_level_item(item, prefix, is_last);
        }
    );
    return std::move(output);
}

auto render_ast_dump(const SourceManager& sources, const SyntaxTree& syntax_tree) noexcept
    -> std::string {
    const auto ast = syntax_tree.view();
    const auto source_view = sources.view(ast.source_id());
    return ASTDumper(ast, source_view.text, source_view.origin).render();
}
