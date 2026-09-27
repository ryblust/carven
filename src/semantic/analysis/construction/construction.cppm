module carven:semantic.analysis.construction;

import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.analysis.construction.requests;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;

class ProgramConstruction final {
public:
    ProgramConstruction(
        ProgramDraft& draft,
        AnalysisCatalogView catalog,
        ImportUsage& usage
    ) noexcept;
    ProgramConstruction(const ProgramConstruction&) = delete;
    ProgramConstruction(ProgramConstruction&&) = delete;
    auto operator=(const ProgramConstruction&) -> ProgramConstruction& = delete;
    auto operator=(ProgramConstruction&&) -> ProgramConstruction& = delete;
    ~ProgramConstruction() = default;
    auto run() noexcept -> AnalysisResult<void>;
    auto construction_requests() noexcept -> ConstructionRequests&;

private:
    auto construct() noexcept -> AnalysisTask<void>;
    ProgramDraft& draft;
    AnalysisCatalogView catalog;
    ConstructionRequests requests;
    DeclResolver declarations;
    BodyBatchElaborator bodies;

    friend class ConstructionRequests;
};
