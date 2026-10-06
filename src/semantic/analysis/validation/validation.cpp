module carven:semantic.analysis.validation.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.validation.context;
import :semantic.analysis.validation;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.contents;
import :semantic.semir.program;
import :semantic.semir.sequence;
import :support.invariant;
import std;

auto verify_semantic_body(const SemIRBody& body, const SemIRProgram& program) noexcept -> void {
    BodyContractVerifier(body, program).verify();
}

auto validate_global_semantic_contracts(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void> {
    auto failure = std::optional<AnalysisFailure>();
    const auto reject = [&](TypeID type, ProgramOriginID origin) noexcept {
        if (!program.type_contents(type).contains_callable_view) {
            return;
        }
        failure =
            diagnostics.error(DiagnosticBuilder(
                                  DiagnosticCode::TypeCallableViewEscape,
                                  "non-owning callable view cannot be stored in a structure or enum"
            )
                                  .primary(program.provenance().source_span(origin))
                                  .build());
    };
    for (const auto [structure, declaration] : program.declarations().structures()) {
        for (const auto& field : declaration.fields) {
            reject(field.type, field.origin);
        }
    }
    for (const auto [enumeration, declaration] : program.declarations().enumerations()) {
        for (const auto enum_case : declaration.cases) {
            const auto case_declaration = program.declarations().enum_case(enum_case);
            if (case_declaration.owner != enumeration) {
                invariant_violation("enum declaration lists a case owned by another enum");
            }
            for (const auto payload : case_declaration.payload_types) {
                reject(payload, case_declaration.origin);
            }
        }
    }
    const auto values = PublishedConstantValues(program);
    for (const auto [type, canonical] : program.types().entries()) {
        const auto* sequence = std::get_if<OwnedSequenceTypeValue>(&canonical.value);
        if (!sequence) {
            continue;
        }
        if (const auto reason = unsupported_sequence_element(values, sequence->element)) {
            auto diagnostic =
                DiagnosticBuilder(DiagnosticCode::TypeSequenceElement, std::string(*reason));
            for (const auto [module, declaration] : program.declarations().modules()) {
                diagnostic.primary(program.provenance().source_span(declaration.origin));
                break;
            }
            failure = diagnostics.error(diagnostic.build());
        }
    }
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    return validate_declaration_surfaces(program, diagnostics);
}
