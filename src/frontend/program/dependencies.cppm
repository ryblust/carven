module carven:frontend.program.dependencies;

import :frontend.ast.storage;
import :source.module_path;
import std;

// Source prerequisites of one document, before name selection and contract checking.
auto source_dependencies(
    ASTView syntax,
    const CanonicalModulePath& module,
    std::string_view source
) noexcept -> std::vector<CanonicalModulePath>;
