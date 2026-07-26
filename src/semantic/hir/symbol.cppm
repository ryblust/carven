module carven:semantic.hir.symbol;

import :semantic.hir.ids;
import std;

struct HIRSymbol final {
    ProgramSpellingID name;
    std::optional<ProgramModuleID> module_id;
    std::optional<HIRTypeID> type;
    std::optional<SymbolID> parent;
    std::optional<SemanticPlaceID> place;
    bool referenced;
};
