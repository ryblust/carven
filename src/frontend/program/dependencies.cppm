module carven:frontend.program.dependencies;

import :frontend.ast.storage;
import :frontend.document;
import :source.module_path;
import :support.function_ref;
import std;

// Source prerequisites of one document, before name selection and contract checking.
auto source_dependencies(
    ASTView syntax,
    const CanonicalModulePath& module_path,
    std::string_view source
) noexcept -> std::vector<CanonicalModulePath>;

// Visits each required module once. Unavailable documents stop traversal;
// unresolved imports remain diagnostics of the selected program.
// The loader and its returned documents are borrowed only during this call.
auto source_dependency_closure(
    std::span<const CanonicalModulePath> roots,
    FunctionRef<std::shared_ptr<const ParsedDocument>(const CanonicalModulePath&) noexcept> load
) noexcept -> std::vector<CanonicalModulePath>;
