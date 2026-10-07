module carven:frontend.program.parse;

import :diagnostics.diagnostic;
import :frontend.ast.decl;
import :frontend.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :support.timing;
import std;

auto parse_program(
    const SourceManager& sources,
    SourceBatch batch,
    TimingOutput timings = {}
) noexcept -> std::expected<SyntaxProgram, Diagnostics>;

// Resolve syntax against the importer's module domain. The returned path is
// independent of the compilation's source and program identities.
enum class ModuleReferenceResolutionError { InvalidCanonicalPath, EscapesModuleDomain };

auto resolve_import_path(
    std::string_view source,
    const CanonicalModulePath& importer_path,
    const ASTModuleReference& reference
) noexcept -> std::expected<CanonicalModulePath, ModuleReferenceResolutionError>;
