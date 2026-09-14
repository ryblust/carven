module carven:semantic.analysis.construction.impl;

import :semantic.analysis.body.context;
import :semantic.analysis.construction;
import :semantic.analysis.construction.requests;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import std;

ProgramConstruction::ProgramConstruction(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage
) noexcept
    : draft(draft),
      catalog(catalog),
      declarations(draft, catalog, usage, *this),
      bodies(draft, catalog, usage, *this) {}

auto ProgramConstruction::run() noexcept -> AnalysisResult<void> {
    auto result = declarations.run();
    if (!result) {
        return result;
    }
    static_cast<void>(analyze_nominal_containment(draft));
    static_cast<void>(diagnose_cpp_api_surface(draft, catalog));
    if (const auto failure = draft.diagnostics().failure()) {
        return std::unexpected(*failure);
    }
    return bodies.run();
}

auto ProgramConstruction::ensure_declaration(
    CatalogSymbolID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisResult<void> {
    return declarations.ensure_available(id, requester, span);
}

auto ProgramConstruction::ensure_function_signature(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisResult<void> {
    return bodies.ensure_function_signature(id, requester, span);
}

auto ProgramConstruction::ensure_function_body(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisResult<BodyID> {
    return bodies.ensure_function_body(id, requester, span);
}

auto ProgramConstruction::ensure_type(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisResult<void> {
    return declarations.prepare_type(type, requester, span);
}
