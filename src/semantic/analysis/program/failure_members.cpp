module carven:semantic.analysis.program.failure_members.impl;

import :diagnostics.builder;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import std;

auto ProgramDraft::require_failure_member(ConstructionTypeRef type, ProgramOriginID origin) noexcept
    -> AnalysisResult<TypeID> {
    type = unwrap_type_alias(*this, type);
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete) {
        const auto canonical = type_copy(*concrete);
        if (std::holds_alternative<StructTypeValue>(canonical.value)
            || std::holds_alternative<EnumTypeValue>(canonical.value)) {
            // Heads can refer back through callable contracts. Copy is checked
            // once all fields are complete, without forcing a declaration cycle.
            storage.failure_members.emplace_back(*concrete, origin);
            return *concrete;
        }
    }
    return std::unexpected(
        diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::EffectThrowType,
                "failure types must be copyable concrete nominal struct or enum types"
            )
                .primary(source_span(origin))
                .build()
        )
    );
}

auto ProgramDraft::validate_failure_members() noexcept -> AnalysisResult<void> {
    require_state(State::Bodies, "validate failure members");
    auto copies = std::map<TypeID, bool>();
    auto failure = std::optional<AnalysisFailure>();
    for (const auto& [type, origin] : storage.failure_members) {
        auto [position, inserted] = copies.try_emplace(type, false);
        if (inserted) {
            position->second = type_supports_copy(*this, type);
        }
        if (!position->second) {
            failure = diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::EffectThrowType,
                    "failure type does not have a proven Copy capability"
                )
                    .primary(source_span(origin), "failure payloads must be copyable")
                    .build()
            );
        }
    }
    return failure ? AnalysisResult<void>(std::unexpected(*failure)) : AnalysisResult<void>();
}
