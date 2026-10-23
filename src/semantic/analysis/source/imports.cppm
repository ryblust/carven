module carven:semantic.analysis.source.imports;

import :frontend.ast.decl;
import :frontend.document;
import :semantic.analysis.source;
import :semantic.visibility;
import :source.module_path;
import :source.text;
import std;

auto source_declaration_visibility(const ASTItem& item) noexcept -> DeclarationVisibility;
// Enumerates admitted source spellings without requiring body construction.
auto source_import_candidates(
    const ParsedDocument& document,
    const CanonicalModulePath& defining,
    const CanonicalModulePath& importer
) noexcept -> std::vector<SourceCompletionCandidate>;

// Compiler-provided modules have no source document, but have known export kinds.
auto source_import_candidates(const CanonicalModulePath& defining) noexcept
    -> std::vector<SourceCompletionCandidate>;

struct SourceImportCompletion final {
    Span replacement;
    std::string prefix;
    std::optional<ASTModuleReference> reference;
    std::vector<std::string> selected;
    // A recognized import with no name slot suppresses ordinary name fallback.
    bool accepts_name;
};

// Lexical import context also covers paths/lists not yet admitted by the parser.
// A reference selects exported names; no reference selects known module paths.
auto source_import_completion(const ParsedDocument& document, std::uint32_t offset) noexcept
    -> std::optional<SourceImportCompletion>;

// A source spelling of a canonical path in the importer's module domain.
auto source_import_reference(
    const CanonicalModulePath& importer,
    const CanonicalModulePath& target,
    bool parent_relative = false
) noexcept -> std::string;
