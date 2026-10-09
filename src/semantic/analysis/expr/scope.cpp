module carven:semantic.analysis.expr.scope.impl;

import :semantic.analysis.expr.scope;
import :semantic.analysis.program;
import :semantic.semir.type;
import std;

auto enum_case_reference_type(
    ProgramDraft& draft,
    TypeID owner,
    std::span<const ConstructionTypeRef> payload
) noexcept -> ConstructionTypeRef {
    if (payload.empty()) {
        return owner;
    }
    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(payload.size());
    for (const auto type : payload) {
        parameters.push_back(
            ConstructionCallableParameter {
                .stage = ParameterStage::Runtime,
                .access = AccessMode::Read,
                .type = type,
            }
        );
    }
    return draft.append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = std::move(parameters),
                .result = owner,
                .failures = draft.add_empty_failure_term(),
            },
        }
    );
}
