module carven:semantic.analysis.validation.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types.contents;
import :semantic.analysis.validation.context;
import :semantic.analysis.validation;
import :semantic.semir.constant;
import :semantic.semir.program;
import :support.invariant;
import std;

auto verify_semantic_body(const SemIRBody& body, const SemIRProgram& program) noexcept -> void {
    BodyContractVerifier(body, program).verify();
}

auto validate_global_semantic_contracts(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics,
    std::span<const TypeContents> types
) noexcept -> AnalysisResult<void> {
    auto failure = std::optional<AnalysisFailure>();
    const auto reject = [&](TypeID type, ProgramOriginID origin) noexcept {
        if (!types[type.index()].callable_view) {
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
    if (failure.has_value()) {
        return std::unexpected(*failure);
    }
    return validate_declaration_surfaces(program, diagnostics);
}
