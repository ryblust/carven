module carven:semantic.analysis.construction.requests;

import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.semir.ids;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.text;

class ConstructionRequests {
public:
    virtual ~ConstructionRequests() = default;
    virtual auto ensure_declaration(
        CatalogSymbolID id,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisResult<void> = 0;
    virtual auto ensure_function_signature(
        FunctionID id,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisResult<void> = 0;
    virtual auto ensure_function_body(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisResult<BodyID> = 0;
    virtual auto ensure_type(
        ConstructionTypeRef type,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisResult<void> = 0;
};
