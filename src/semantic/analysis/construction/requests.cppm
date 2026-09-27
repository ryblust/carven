module carven:semantic.analysis.construction.requests;

import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.semir.ids;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.text;

class ProgramConstruction;

// Borrowed completion port. Its coordinator outlives every request and continuation.
class ConstructionRequests final {
public:
    auto ensure_declaration(CatalogSymbolID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;
    auto ensure_function_signature(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;
    auto ensure_function_body(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<BodyID>;
    auto ensure_type(ConstructionTypeRef type, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

private:
    explicit ConstructionRequests(ProgramConstruction& owner) noexcept;
    ProgramConstruction& owner;

    friend class ProgramConstruction;
};
