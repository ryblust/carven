module carven:semantic.analysis.construction;

import :semantic.analysis.body.context;
import :semantic.analysis.catalog;
import :semantic.analysis.construction.requests;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;

class ProgramConstruction final : public ConstructionRequests {
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
    auto ensure_declaration(CatalogSymbolID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisResult<void> override;
    auto ensure_function_signature(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisResult<void> override;
    auto ensure_function_body(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisResult<BodyID> override;
    auto ensure_type(ConstructionTypeRef type, ProgramModuleID requester, Span span) noexcept
        -> AnalysisResult<void> override;

private:
    ProgramDraft& draft;
    AnalysisCatalogView catalog;
    DeclResolver declarations;
    BodyBatchElaborator bodies;
};
