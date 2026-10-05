module carven:editor.document;

import :diagnostics.diagnostic;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :source.manager;
import :source.text;
import std;

class EditorDocumentSource final {
public:
    static auto create(std::string document, std::string text) noexcept
        -> std::expected<std::shared_ptr<const EditorDocumentSource>, SourceLoadError>;
    auto source() const noexcept -> SourceView;
    auto sources() const noexcept -> const SourceManager&;

private:
    EditorDocumentSource(SourceManager sources, SourceID id) noexcept;

    SourceManager source_manager;
    SourceID source_id;
};

class EditorDocumentSyntax final {
public:
    EditorDocumentSyntax(
        std::shared_ptr<const EditorDocumentSource> source,
        std::optional<SyntaxTree> syntax,
        Diagnostics diagnostics
    ) noexcept;
    auto source() const noexcept -> SourceView;
    auto sources() const noexcept -> const SourceManager&;
    // Complete syntax remains unavailable whenever parsing reports errors.
    auto syntax() const noexcept -> std::optional<ASTView>;
    // Includes complete retained declarations around recovered top-level errors.
    // Lexical, delimiter, and initial import failures still return no tree.
    auto recovered_syntax() const noexcept -> std::optional<ASTView>;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;

private:
    // The source owner remains alive for all AST and diagnostic source identities.
    std::shared_ptr<const EditorDocumentSource> input;
    std::optional<SyntaxTree> tree;
    Diagnostics findings;
};

auto parse_editor_document(std::shared_ptr<const EditorDocumentSource> source) noexcept
    -> std::shared_ptr<const EditorDocumentSyntax>;
