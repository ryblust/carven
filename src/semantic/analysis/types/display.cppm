module carven:semantic.analysis.types.display;

import :semantic.analysis.program;
import :semantic.semir.type;
import std;

// Spells a type as source would, for diagnostic text. Nominal names are
// unqualified; an inferred callable or closure is described by its signature.
auto type_display_name(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> std::string;
