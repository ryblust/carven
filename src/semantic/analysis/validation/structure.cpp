module carven:semantic.analysis.validation.structure.impl;

import :semantic.analysis.validation.invariants;
import :semantic.analysis.validation.context;

auto verify_semantic_structure(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return validate_ownership(program);
}
