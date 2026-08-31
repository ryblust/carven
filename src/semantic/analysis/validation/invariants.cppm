module carven:semantic.analysis.validation.invariants;

import :semantic.analysis.session.read;
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

auto verify_semantic_program(SemanticProgramView semantic) noexcept
    -> std::expected<void, SemanticProgramError>;

auto verify_semantic_draft_for_testing(SemanticDraftView semantic) noexcept
    -> std::expected<void, SemanticProgramError>;

auto verify_semantic_structure(SemanticDraftView semantic) noexcept
    -> std::expected<void, SemanticProgramError>;
