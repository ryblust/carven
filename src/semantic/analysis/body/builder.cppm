module carven:semantic.analysis.body.builder;

import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :support.invariant;
import std;

struct BoundStorage final {
    LocalBindingID binding;
};

struct PlaceExpression final {
    std::optional<LocalBindingID> root;
    SemanticExpression expression;
};

class BodyBuilder final {
public:
    BodyBuilder(BodyReservation reservation, ProgramDraft& draft) noexcept;
    auto identity() const noexcept -> BodyIdentity;
    auto id() const noexcept -> BodyID;
    auto kind() const noexcept -> BodyKind;
    auto set_lifetime(LifetimeRegionID lifetime) noexcept -> void;
    auto lifetime() const noexcept -> LifetimeRegionID;
    auto add_lifetime_region(
        std::optional<LifetimeRegionID>,
        LifetimeRegionKind,
        ProgramOriginID
    ) noexcept -> LifetimeRegionID;
    auto add_parameter(
        ProgramSpellingID,
        ConstructionTypeRef,
        LifetimeRegionID,
        AccessMode,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    auto add_capture(
        ProgramSpellingID,
        ConstructionTypeRef,
        LifetimeRegionID,
        CaptureMode,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    auto add_owner_binding(
        ProgramSpellingID,
        ConstructionTypeRef,
        LifetimeRegionID,
        bool,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    auto add_pattern(ElaboratedPattern) noexcept -> PatternID;
    auto pattern_copy(PatternID) const noexcept -> ElaboratedPattern;
    auto pattern_table() const noexcept -> const MutableBodyTable<ElaboratedPattern, PatternID>&;
    auto place_access(const PlaceExpression&) const noexcept -> AccessMode;
    auto binding_expression(LocalBindingID) noexcept -> PlaceExpression;
    auto make_place(
        std::optional<LocalBindingID>,
        ConstructionTypeRef,
        SemanticExpressionValue,
        ProgramOriginID
    ) noexcept -> PlaceExpression;
    auto cpp_place(
        PlaceExpression,
        ConstructionTypeRef,
        CppOperation,
        std::vector<SemCallArgument>,
        ProgramOriginID
    ) noexcept -> PlaceExpression;
    auto callable_expression(CallableID, ProgramOriginID) noexcept -> SemanticExpression;
    auto make_expression(
        ConstructionTypeRef,
        LifetimeRegionID,
        ProgramOriginID,
        SemanticExpressionValue,
        std::optional<ConstantID> = std::nullopt
    ) noexcept -> SemanticExpression;
    auto finish(SemanticRegion) && noexcept -> StructuredBodyDraft;

private:
    auto add_binding(
        ProgramSpellingID,
        ConstructionTypeRef,
        LifetimeRegionID,
        BindingStorage,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    ProgramDraft& draft;
    BodyIdentity body_identity;
    BodyID body_id;
    BodyKind body_kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs body_inputs;
    MutableBodyTable<LifetimeRegion, LifetimeRegionID> lifetime_regions;
    MutableBodyTable<ElaboratedLocalBinding, LocalBindingID> bindings;
    MutableBodyTable<ElaboratedPattern, PatternID> patterns;
    std::optional<LifetimeRegionID> active_lifetime;
};
