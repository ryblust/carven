module carven:workspace.analysis.interop.impl;

import :frontend.ast.decl;
import :frontend.ast.interop;
import :frontend.ast.tree;
import :source.text;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {

auto contains(Span range, std::uint32_t offset) noexcept -> bool {
    return range.start() <= offset && offset < range.end();
}

auto native_path(SourceView source, std::span<const Span> components) noexcept -> std::string {
    auto result = std::string();
    for (const auto component : components) {
        result += "::";
        result += slice(source.text, component);
    }
    return result;
}

auto boundary_hover(
    std::string_view document,
    Span range,
    std::string description,
    std::string notes
) noexcept -> WorkspaceHoverInformation {
    return {
        .location = {std::string(document), range},
        .type = std::nullopt,
        .entity = std::nullopt,
        .type_description = {},
        .callable = std::nullopt,
        .description = std::move(description),
        .documentation = std::move(notes)
    };
}

} // namespace

auto WorkspaceAnalysisSnapshot::interop_hover(
    const WorkspaceSemanticAnalysis& analysis,
    std::string_view document,
    std::uint32_t offset,
    std::optional<WorkspaceHoverInformation> hover
) const noexcept -> std::optional<WorkspaceHoverInformation> {
    auto target_document = std::string(document);
    auto target = std::optional<Span>();
    if (hover && hover->entity) {
        if (const auto* entity = std::get_if<SourceSpan>(&*hover->entity)) {
            target_document = analysis.sources().view(entity->source_id).origin;
            target = entity->span;
        }
    }
    const auto parsed = syntax(target_document);
    const auto tree = parsed ? parsed->result->recovered_syntax() : std::nullopt;
    if (!tree) {
        return hover;
    }
    const auto source = parsed->result->source();
    for (const auto& item : tree->items()) {
        const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
        const auto* imported =
            function ? std::get_if<ASTCppImportForm>(&function->implementation) : nullptr;
        if (!imported) {
            continue;
        }
        const auto provider = imported->target
            ? native_path(source, imported->target->components)
            : "::" + std::string(slice(source.text, function->name_span));
        const auto notes = "C++ provider: " + provider
            + "\nSignature declared in Carven. The C++ compiler checks the generated provider call and resolves overloads.";
        if (hover && target == function->name_span) {
            hover->documentation = notes;
            return hover;
        }
        if (!hover
            && target_document == document
            && imported->target
            && contains(imported->target->span, offset)) {
            return boundary_hover(document, imported->target->span, provider, notes);
        }
    }
    if (hover || target_document != document) {
        return hover;
    }
    for (const auto& header : tree->ast_module().cpp_header_imports) {
        if (contains(header.span, offset)) {
            return boundary_hover(
                document,
                header.span,
                std::string(slice(source.text, header.span)),
                "C++ header import. The header is included for C++ compilation; its declarations are not parsed by Carven."
            );
        }
    }
    for (const auto& fragment : tree->ast_module().cpp_source_fragments) {
        const auto marker =
            Span::from_bounds(fragment.form_span.start(), fragment.payload_span.start());
        if (contains(marker, offset)) {
            return boundary_hover(
                document,
                marker,
                "#[cpp]",
                "C++ source fragment. Its contents are preserved verbatim and checked by the C++ compiler; names inside it are not Carven declarations."
            );
        }
    }
    return hover;
}
