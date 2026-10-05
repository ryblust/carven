module carven:semantic.analysis.types.display;

import :semantic.semir.type;
import std;

class ProgramDraft;
class SemIRProgram;

// Spells source types for diagnostics and editor results. Nominal names are
// unqualified; an inferred callable or closure is described by its signature.
auto type_display_name(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> std::string;

// Published IDs are interpreted while their program owner is alive.
auto type_display_name(const SemIRProgram& program, TypeID type) noexcept -> std::string;
auto type_display_name(BuiltinType type) noexcept -> std::string;
