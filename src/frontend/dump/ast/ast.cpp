module carven:frontend.dump.ast.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.interop;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.dump.ast;
import :frontend.dump.text;
import :frontend.lex.token;
import :source.manager;
import :source.text;
import :support.visit;
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
    using Import = std::variant<ASTModuleImportID, const ASTCppHeaderImport*>;
    auto imports = std::vector<Import>();
    imports.reserve(ast_module.module_imports.size() + ast_module.cpp_header_imports.size());
    for (const auto module_import : ast_module.module_imports) {
        imports.emplace_back(module_import);
    }
    for (const auto& header : ast_module.cpp_header_imports) {
        imports.emplace_back(std::addressof(header));
    }
    const auto import_span = [&](const Import& value) noexcept {
        return std::visit(
            Overloaded {
                [&](ASTModuleImportID id) noexcept { return ast.module_import(id).span; },
                [](const ASTCppHeaderImport* header) static noexcept { return header->span; },
            },
            value
        );
    };
    std::ranges::sort(imports, {}, [&](const Import& value) noexcept {
        return import_span(value).start();
    });
    render_list(
        {},
        false,
        "imports",
        imports,
        [&](const Import& value, std::string_view prefix, bool is_last) noexcept {
            std::visit(
                Overloaded {
                    [&](ASTModuleImportID declaration) noexcept {
                        render_module_import(declaration, prefix, is_last);
                    },
                    [&](const ASTCppHeaderImport* header) noexcept {
                        render_cpp_header_import(*header, prefix, is_last);
                    },
                },
                value
            );
        }
    );
    render_list(
        {},
        false,
        "cpp_source_fragments",
        ast_module.cpp_source_fragments,
        [&](const ASTCppSourceFragment& fragment, std::string_view prefix, bool is_last) noexcept {
            render_cpp_source_fragment(fragment, prefix, is_last);
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
