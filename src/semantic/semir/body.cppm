module carven:semantic.semir.body;

import :semantic.semir.constant;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.provenance.ids;
import :support.invariant;
import :support.visit;
import std;

enum class BodyKind {
    Function,
    Closure,
    Test,
};

struct Scope final {
    std::optional<ScopeID> parent;
    ProgramOriginID origin;
};

enum class LifetimeRegionKind {
    Lexical,
    FullExpression,
    Static,
};

struct LifetimeRegion final {
    std::optional<LifetimeRegionID> parent;
    LifetimeRegionKind kind;
    ProgramOriginID origin;
};

class ScopeTree final {
public:
    explicit ScopeTree(ImmutableBodyTable<Scope, ScopeID> rows) noexcept
        : scope_rows(std::move(rows)) {}
    ScopeTree(const ScopeTree&) = delete;
    ScopeTree(ScopeTree&&) = default;
    ~ScopeTree() = default;

    auto operator=(const ScopeTree&) -> ScopeTree& = delete;
    auto operator=(ScopeTree&&) -> ScopeTree& = default;

    auto owner() const noexcept -> BodyIdentity { return scope_rows.owner(); }
    auto contains(ScopeID id) const noexcept -> bool { return scope_rows.contains(id); }
    auto scope(ScopeID id) const noexcept -> const Scope& { return scope_rows.get(id); }
    auto entries() const noexcept -> IDTableEntries<ScopeID, Scope, BodyIdentity> {
        return scope_rows.entries();
    }

private:
    ImmutableBodyTable<Scope, ScopeID> scope_rows;
};

class LifetimeRegionTree final {
public:
    explicit LifetimeRegionTree(ImmutableBodyTable<LifetimeRegion, LifetimeRegionID> rows) noexcept
        : region_rows(std::move(rows)) {}
    LifetimeRegionTree(const LifetimeRegionTree&) = delete;
    LifetimeRegionTree(LifetimeRegionTree&&) = default;
    ~LifetimeRegionTree() = default;

    auto operator=(const LifetimeRegionTree&) -> LifetimeRegionTree& = delete;
    auto operator=(LifetimeRegionTree&&) -> LifetimeRegionTree& = default;

    auto owner() const noexcept -> BodyIdentity { return region_rows.owner(); }
    auto contains(LifetimeRegionID id) const noexcept -> bool { return region_rows.contains(id); }
    auto region(LifetimeRegionID id) const noexcept -> const LifetimeRegion& {
        return region_rows.get(id);
    }
    auto is_ancestor(LifetimeRegionID ancestor, LifetimeRegionID descendant) const noexcept
        -> bool {
        if (!contains(ancestor) || !contains(descendant)) {
            invariant_violation("lifetime relation received a foreign region");
        }
        auto current = std::optional {descendant};
        while (current.has_value()) {
            if (*current == ancestor) {
                return true;
            }
            current = region(*current).parent;
        }
        return false;
    }
    auto outlives(LifetimeRegionID outer, LifetimeRegionID inner) const noexcept -> bool {
        if (!contains(outer) || !contains(inner)) {
            invariant_violation("lifetime relation received a foreign region");
        }
        return region(outer).kind == LifetimeRegionKind::Static || is_ancestor(outer, inner);
    }
    auto entries() const noexcept
        -> IDTableEntries<LifetimeRegionID, LifetimeRegion, BodyIdentity> {
        return region_rows.entries();
    }

private:
    ImmutableBodyTable<LifetimeRegion, LifetimeRegionID> region_rows;
};

enum class CaptureMode {
    Value,
    Write,
};

struct OwnerBindingStorage final {
    bool writable;
};
struct ParameterBindingStorage final {
    AccessMode access;
};
struct CaptureBindingStorage final {
    CaptureMode mode;
};

using BindingStorage =
    std::variant<OwnerBindingStorage, ParameterBindingStorage, CaptureBindingStorage>;

struct LocalBinding final {
    ProgramSpellingID name;
    TypeID type;
    ScopeID scope;
    LifetimeRegionID lifetime;
    BindingStorage storage;
    ProgramOriginID origin;
};

struct ElaboratedLocalBinding final {
    ProgramSpellingID name;
    ConstructionTypeRef type;
    ScopeID scope;
    LifetimeRegionID lifetime;
    BindingStorage storage;
    ProgramOriginID origin;
};

struct WildcardPattern final {};
struct LiteralPattern final {
    ConstantID constant;
};
struct OrPattern final {
    std::vector<PatternID> alternatives;
};
struct TypeConstraintPattern final {
    TypeID type;
};
struct BindingPattern final {
    LocalBindingID binding;
};
struct EnumCasePattern final {
    EnumCaseID enum_case;
    std::vector<PatternID> payload;
};

using PatternValue = std::variant<
    WildcardPattern,
    LiteralPattern,
    OrPattern,
    TypeConstraintPattern,
    BindingPattern,
    EnumCasePattern>;

struct Pattern final {
    TypeID type;
    PatternValue value;
    ProgramOriginID origin;
};

struct ElaboratedTypeConstraintPattern final {
    ConstructionTypeRef type;
};
using ElaboratedPatternValue = std::variant<
    WildcardPattern,
    LiteralPattern,
    OrPattern,
    ElaboratedTypeConstraintPattern,
    BindingPattern,
    EnumCasePattern>;
struct ElaboratedPattern final {
    ConstructionTypeRef type;
    ElaboratedPatternValue value;
    ProgramOriginID origin;
};

struct ProvenInBounds final {};
struct RuntimeCheckedBounds final {};
using ArrayBoundsPolicy = std::variant<ProvenInBounds, RuntimeCheckedBounds>;

struct FieldProjection final {
    StructID owner;
    std::uint32_t field_index;
};

struct IntegerLiteral final {
    IntegerConstant value;
};
struct F32Literal final {
    float value;
};
struct F64Literal final {
    double value;
};
struct BooleanLiteral final {
    bool value;
};
struct CharacterLiteral final {
    char32_t scalar;
};
struct StringLiteral final {
    ProgramSpellingID bytes;
};
using LiteralValue = std::variant<
    IntegerLiteral,
    F32Literal,
    F64Literal,
    BooleanLiteral,
    CharacterLiteral,
    StringLiteral>;


enum class CastKind {
    Identity,
    IntegerToInteger,
    IntegerToBool,
    BoolToInteger,
    IntegerToFloating,
    FloatingWiden,
    EnumToInteger,
};

enum class TextIntrinsic {
    Len,
    IsEmpty,
    Bytes,
    Chars,
};

enum class TestReportKind {
    Check,
    Require,
    Fail,
};

struct CatchAllPattern final {};

struct BodyInputs final {
    std::vector<LocalBindingID> parameters;
    std::vector<LocalBindingID> captures;
};
