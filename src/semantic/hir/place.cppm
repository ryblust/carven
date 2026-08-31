module carven:semantic.hir.place;

import :semantic.hir.ids;
import std;

struct SemanticScope final {
    std::optional<SemanticScopeID> parent;
};

enum class SemanticBindingStorage {
    Owner,
    Borrow,
    Compiler,
};

struct SemanticBindingCapabilities final {
    bool write;
    bool take;
};

struct SemanticBindingFacts final {
    SemanticBindingStorage storage;
    SemanticBindingCapabilities capabilities;
    SemanticScopeID scope;
    std::uint32_t declaration_order;
};

enum class SemanticPlaceAccess {
    Read,
    Write,
    ReadWrite,
    Take,
};

struct SemanticFieldProjection final {
    StructID owner;
    std::uint32_t field_index;
};

struct SemanticIndexProjection final {};

using SemanticPlaceProjection = std::variant<SemanticFieldProjection, SemanticIndexProjection>;

struct SemanticPlaceUse final {
    SymbolID root;
    std::vector<SemanticPlaceProjection> projections;
    SemanticPlaceAccess access;
};

struct EvaluationEffect final {
    std::vector<SymbolID> reads;
    std::vector<SymbolID> writes;
    std::vector<SymbolID> takes;
    bool opaque_boundary;
};
