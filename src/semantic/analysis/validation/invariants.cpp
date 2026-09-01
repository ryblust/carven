module carven:semantic.analysis.validation.invariants.impl;

import :semantic.analysis.validation.invariants;
import :semantic.analysis.validation.context;
import std;

auto verify_semantic_program(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    if (auto tables = validate_tables(program); !tables.has_value()) {
        return tables;
    }
    if (auto ownership = validate_ownership(program); !ownership.has_value()) {
        return ownership;
    }
    if (auto relations = validate_relations(program); !relations.has_value()) {
        return relations;
    }
    return validate_flow(program);
}

auto verify_semantic_draft_for_testing(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    if (auto tables = validate_tables(program); !tables.has_value()) {
        return tables;
    }
    if (auto ownership = validate_ownership(program); !ownership.has_value()) {
        return ownership;
    }
    if (auto relations = validate_relations(program); !relations.has_value()) {
        return relations;
    }
    return validate_flow(program);
}
