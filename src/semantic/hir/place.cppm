module carven:semantic.hir.place;

import :semantic.hir.ids;
import std;

struct SemanticScope final {
    std::optional<SemanticScopeID> parent;
};

enum class SemanticPlaceStorage {
    Owner,
    Borrow,
    Compiler,
};

struct SemanticPlaceCapabilities final {
    bool write;
    bool take;
};

struct SemanticPlace final {
    std::optional<SymbolID> symbol;
    HIRTypeID type;
    SemanticPlaceStorage storage;
    SemanticPlaceCapabilities capabilities;
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
    HIRTypeID result_type;
};

struct SemanticIndexProjection final {
    HIRTypeID result_type;
};

using SemanticPlaceProjection = std::variant<SemanticFieldProjection, SemanticIndexProjection>;

struct SemanticPlaceUse final {
    SemanticPlaceID root;
    std::vector<SemanticPlaceProjection> projections;
    SemanticPlaceAccess access;
};

struct EvaluationEffect final {
    std::vector<SemanticPlaceID> reads;
    std::vector<SemanticPlaceID> writes;
    std::vector<SemanticPlaceID> takes;
    bool opaque_boundary;
    bool may_terminate;
};
