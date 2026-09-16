module carven:semantic.semir.body;

import :semantic.semir.constant;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.slice;
import :semantic.semir.table;
import :semantic.semir.text;
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

enum class LifetimeRegionKind {
    Lexical,
    FullExpression,
};

struct LifetimeRegion final {
    std::optional<LifetimeRegionID> parent;
    LifetimeRegionKind kind;
    ProgramOriginID origin;
};

class LifetimeRegionTree final {
public:
    explicit LifetimeRegionTree(ImmutableBodyTable<LifetimeRegion, LifetimeRegionID> rows) noexcept;
    LifetimeRegionTree(const LifetimeRegionTree&) = delete;
    LifetimeRegionTree(LifetimeRegionTree&&) = default;
    ~LifetimeRegionTree() = default;
    auto operator=(const LifetimeRegionTree&) -> LifetimeRegionTree& = delete;
    auto operator=(LifetimeRegionTree&&) -> LifetimeRegionTree& = delete;
    auto owner() const noexcept -> BodyIdentity;
    auto contains(LifetimeRegionID id) const noexcept -> bool;
    auto region(LifetimeRegionID id) const noexcept -> const LifetimeRegion&;
    auto outlives(LifetimeRegionID outer, LifetimeRegionID inner) const noexcept -> bool;
    auto entries() const noexcept -> IDTableEntries<LifetimeRegionID, LifetimeRegion, BodyIdentity>;

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
    LifetimeRegionID lifetime;
    BindingStorage storage;
    ProgramOriginID origin;
};

struct ElaboratedLocalBinding final {
    ProgramSpellingID name;
    ConstructionTypeRef type;
    LifetimeRegionID lifetime;
    BindingStorage storage;
    ProgramOriginID origin;
};

struct WildcardPattern final {};

struct LiteralPattern final {
    ConstantID constant;
};

// A missing bound is unbounded. A present bound without a constant is evaluated
// through the owning arm's structured expression at this pattern's attempt.
struct RangePatternBound final {
    std::optional<ConstantID> constant;
};

struct RangePattern final {
    std::optional<RangePatternBound> begin;
    std::optional<RangePatternBound> end;
    bool inclusive;
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
    RangePattern,
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
    RangePattern,
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

using IndexBoundsPolicy = std::variant<ProvenInBounds, RuntimeCheckedBounds>;

struct FieldProjection final {
    StructID owner;
    std::uint32_t field_index;
};

enum class CastKind {
    Identity,
    PointerRead,
    IntegerToInteger,
    IntegerToBool,
    BoolToInteger,
    IntegerToFloating,
    FloatingWiden,
    EnumToInteger,
    CharToU32,
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
