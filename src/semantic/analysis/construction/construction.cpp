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
      requests(*this),
      static_stage(draft, requests),
      declarations(draft, catalog, usage, requests),
      bodies(draft, catalog, usage, requests) {}

auto ProgramConstruction::run() noexcept -> AnalysisResult<void> {
    return construct().run();
}

auto ProgramConstruction::construct() noexcept -> AnalysisTask<void> {
    auto result = (co_await declarations.run());
    if (!result) {
        co_return result;
    }
    static_cast<void>(analyze_nominal_containment(draft));
    static_cast<void>(diagnose_cpp_api_surface(draft, catalog));
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    co_return (co_await bodies.run());
}

auto ProgramConstruction::construction_requests() noexcept -> ConstructionRequests& {
    return requests;
}
