module carven:semantic.analysis.generics.snapshot.impl;

import :semantic.analysis.generics.snapshot;
import :support.invariant;
import std;

auto GenericAnalysisSnapshot::generic_type(GenericTypeID type) const noexcept
    -> const GenericTypeExpression& {
    if (definitions.empty()
        || type.index() >= types.size()
        || definitions.front().visit([](const auto& definition) static noexcept {
               return definition.contract.module_id.owner();
           })
            != type.owner()) {
        invariant_violation("generic snapshot type has a foreign or invalid identity");
    }
    return types[type.index()];
}

auto GenericAnalysisSnapshot::definition(GenericDeclarationID id) const noexcept
    -> const GenericDeclarationDefinition& {
    if (id.index() >= definitions.size()
        || definitions[id.index()].visit([](const auto& definition) static noexcept {
               return definition.contract.module_id.owner();
           })
            != id.owner()) {
        invariant_violation("generic snapshot declaration has a foreign or invalid identity");
    }
    return definitions[id.index()];
}
