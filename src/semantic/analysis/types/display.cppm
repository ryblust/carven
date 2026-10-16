module carven:semantic.analysis.types.display;

import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.semir.type;
import std;

// Spells types for diagnostics with module-qualified nominal names and callable
// signatures. A construction catalog supplies source names before declarations
// are complete; displaying a name does not request declaration completion.
auto type_display_name(
    const ProgramDraft& draft,
    ConstructionTypeRef type,
    std::optional<AnalysisCatalogView> catalog = std::nullopt
) noexcept -> std::string;
