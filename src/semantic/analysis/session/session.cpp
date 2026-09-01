module carven:semantic.analysis.session.impl;

import :semantic.analysis.session;
import :semantic.analysis.validation.invariants;
import :support.invariant;
import std;

SemanticDraft::SemanticDraft(CompilationProvenance provenance) noexcept
    : provenance_builder(std::move(provenance)) {}

SemanticSession::SemanticSession(CompilationProvenance provenance) noexcept
    : semantic_draft(std::move(provenance)) {}

auto SemanticSession::draft() noexcept -> SemanticDraft& {
    return semantic_draft;
}
auto SemanticSession::draft() const noexcept -> const SemanticDraft& {
    return semantic_draft;
}

auto SemanticSession::finish() && noexcept -> SemanticProgram {
    auto& draft = semantic_draft;
    if (draft.storage.modules.size() != draft.provenance_builder.module_count()) {
        invariant_violation("semantic modules are not aligned with program provenance");
    }
    if (draft.symbol_construction.size() != draft.reserved_symbol_count
        || !draft.function_slots.empty()
        || !draft.struct_slots.empty()
        || !draft.enum_slots.empty()
        || !draft.enum_case_slots.empty()) {
        invariant_violation("semantic reserved entity slots were not completed before seal");
    }
    if (!draft.expression_place_uses.empty() || !draft.callable_failure_input_storage.empty()) {
        invariant_violation("transient semantic analysis inputs survived flow freeze");
    }

    draft.storage.type_index.clear();
    draft.storage.failure_set_index.clear();
    draft.storage.callable_signature_index.clear();

    auto published = SemanticProgramStorage();
    published.types = std::move(draft.storage.types);
    published.expressions = std::move(draft.storage.expressions);
    published.expression_controls = std::move(draft.storage.expression_controls);
    published.evaluation_effects = std::move(draft.storage.evaluation_effects);
    published.place_uses = std::move(draft.storage.place_uses);
    published.try_facts = std::move(draft.storage.try_facts);
    published.constants = std::move(draft.storage.constants);
    published.statements = std::move(draft.storage.statements);
    published.patterns = std::move(draft.storage.patterns);
    published.blocks = std::move(draft.storage.blocks);
    published.block_controls = std::move(draft.storage.block_controls);
    published.scopes = std::move(draft.storage.scopes);
    published.bindings = std::move(draft.storage.bindings);
    published.functions = std::move(draft.storage.functions);
    published.tests = std::move(draft.storage.tests);
    published.structures = std::move(draft.storage.structures);
    published.enumerations = std::move(draft.storage.enumerations);
    published.enum_cases = std::move(draft.storage.enum_cases);
    published.modules = std::move(draft.storage.modules);
    published.failure_sets = std::move(draft.storage.failure_sets);
    published.callable_signatures = std::move(draft.storage.callable_signatures);
    published.callables = std::move(draft.storage.callables);
    published.callable_flows = std::move(draft.storage.callable_flows);
    published.struct_capabilities = std::move(draft.storage.struct_capabilities);
    published.enum_capabilities = std::move(draft.storage.enum_capabilities);
    published.nominal_containment = std::move(draft.storage.nominal_containment);

    for (auto& state : draft.symbol_construction) {
        published.symbols.add(std::move(state.symbol));
    }
    draft.symbol_construction.clear();
    for (auto& body : draft.body_slots) {
        if (!body.has_value()) {
            invariant_violation("semantic body was not defined before publication");
        }
        published.bodies.add(std::move(*body));
    }
    draft.body_slots.clear();

    auto provenance = std::move(draft.provenance_builder).finish();
    const auto published_view = SemanticProgramView(provenance.view(), published);
    if (const auto verified = verify_semantic_program(published_view); !verified.has_value()) {
        invariant_violation(verified.error().message);
    }
    return SemanticProgram(std::move(provenance), std::move(published));
}
