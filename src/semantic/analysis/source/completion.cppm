module carven:semantic.analysis.source.completion;

import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.analysis.source;
import :semantic.semir.type;
import std;

// Enumerate the same selected bindings used by semantic name resolution. This
// performs no declaration/body requests and can run during failed construction.
auto source_name_candidates(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ProgramModuleID module_id,
    bool types_only
) noexcept -> std::vector<SourceCompletionCandidate>;
auto source_intrinsic_members(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::vector<SourceCompletionCandidate>;
