module carven:semantic.analysis.expr.scope.impl;

import :semantic.analysis.expr.scope;
import :semantic.analysis.program;
import :semantic.analysis.operations;
import :semantic.semir.type;
import std;

auto enum_case_reference_type(
    ProgramDraft& draft,
    ConstructionTypeRef owner,
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
    return make_callable_view_type(
        draft,
        std::move(parameters),
        owner,
        draft.add_empty_failure_term()
    );
}
