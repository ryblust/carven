module carven:semantic.analysis.construction.requests.impl;

import :semantic.analysis.construction;
import :semantic.analysis.construction.requests;

ConstructionRequests::ConstructionRequests(ProgramConstruction& owner) noexcept
    : owner(owner) {}

auto ConstructionRequests::ensure_declaration(
    CatalogSymbolID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.declarations.ensure_available(id, requester, span);
}

auto ConstructionRequests::ensure_function_signature(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.bodies.ensure_function_signature(id, requester, span);
}

auto ConstructionRequests::ensure_function_body(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<BodyID> {
    return owner.bodies.ensure_function_body(id, requester, span);
}

auto ConstructionRequests::ensure_type(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    return owner.declarations.prepare_type(type, requester, span);
}
