module carven:semantic.analysis.validation.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types.contents;
import :semantic.analysis.validation;
import :semantic.analysis.validation.context;
import :semantic.semir.constant;
import :support.invariant;
import :support.visit;
import std;

auto verify_semantic_body(
    const SemIRBody& body,
    ProgramDraft& draft,
    std::span<const SemIRBody> bodies
) noexcept -> void {
    validation_detail::BodyContractVerifier(body, draft, bodies).verify();
}

auto validate_global_semantic_contracts(ProgramDraft& draft) noexcept -> AnalysisResult<void> {
    auto contents = TypeContentsQuery(draft);
    auto failure = std::optional<AnalysisFailure>();
    const auto reject = [&](ConstructionTypeRef construction, ProgramOriginID origin) noexcept {
        const auto type = draft.concrete_type(construction);
        if (!contents.contains_view(type)) {
            return;
        }
        failure = draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::TypeCallableViewEscape,
                "non-owning callable view cannot be stored in a structure or enum"
            )
                .primary(draft.source_span(origin))
                .build()
        );
    };
    for (const auto structure : draft.struct_declaration_ids()) {
        const auto declaration = draft.construction_struct_declaration_copy(structure);
        for (const auto& field : declaration.fields) {
            reject(field.type, field.origin);
        }
    }
    for (const auto enumeration : draft.enum_declaration_ids()) {
        const auto declaration = draft.construction_enum_declaration_copy(enumeration);
        for (const auto enum_case : declaration.cases) {
            const auto case_declaration = draft.construction_enum_case_declaration_copy(enum_case);
            if (case_declaration.owner != enumeration) {
                invariant_violation("enum declaration lists a case owned by another enum");
            }
            for (const auto payload : case_declaration.payload_types) {
                reject(payload, case_declaration.origin);
            }
        }
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    return {};
}
