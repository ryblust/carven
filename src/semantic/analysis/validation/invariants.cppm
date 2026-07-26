module carven:semantic.analysis.validation.invariants;

import :semantic.hir;
import std;

enum class SemanticProgramErrorKind {
    InvalidReference,
    InvalidOwnership,
    InvalidType,
    InvalidScope,
    InvalidPlace,
    InvalidContract,
};

struct SemanticProgramError final {
    SemanticProgramErrorKind kind;
    std::string message;
};

auto verify_semantic_program(const SemanticConstruction& semantic) noexcept
    -> std::expected<void, SemanticProgramError>;

auto verify_semantic_structure(const SemanticConstruction& semantic) noexcept
    -> std::expected<void, SemanticProgramError>;
