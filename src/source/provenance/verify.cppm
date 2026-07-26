module carven:source.provenance.verify;

import :source.provenance;
import std;

enum class CompilationProvenanceErrorKind {
    DuplicateSource,
    DuplicateModuleSource,
    DuplicateModulePath,
    UnorderedModules,
    MissingModuleSource,
    DuplicateSpelling,
    MissingOriginSource,
    InvalidOriginSpan,
    InvalidParentOrigin,
};

struct CompilationProvenanceError final {
    CompilationProvenanceErrorKind kind;
    std::string message;
};

auto verify_compilation_provenance(CompilationProvenanceView provenance) noexcept
    -> std::expected<void, CompilationProvenanceError>;
