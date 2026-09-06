module carven:semantic.analysis.body.builder;

import :semantic.analysis.body.inputs;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :support.invariant;
import std;

struct BoundStorage final {
    LocalBindingID binding;
    PlaceHandle root_place;
};
struct UnresolvedCallableBorrowOp final {
    expression_construction::CallableSource backing;
    ConstructionTypeRef target_type;
    FailureTermID source_failure_term_id;
    FailureTermID target_failure_term_id;
    LifetimeRegionID loan_lifetime;
};

// Handles exist only while resolving an expression. A parent consumes its
// children's owned trees, and publication contains no handle table.
class BodyBuilder final {
public:
    BodyBuilder(BodyReservation&& reservation, ProgramDraft& draft) noexcept;
    auto identity() const noexcept -> BodyIdentity { return body_identity; }
    auto id() const noexcept -> BodyID { return body_id; }
    auto kind() const noexcept -> BodyKind { return body_kind; }
    auto set_lifetime(LifetimeRegionID lifetime) noexcept -> void { active_lifetime = lifetime; }
    auto lifetime() const noexcept -> LifetimeRegionID { return active_lifetime.value(); }
    auto add_scope(std::optional<ScopeID>, ProgramOriginID) noexcept -> ScopeID;
    auto add_lifetime_region(
        std::optional<LifetimeRegionID>,
        LifetimeRegionKind,
        ProgramOriginID
    ) noexcept -> LifetimeRegionID;
    auto add_parameter(
        ProgramSpellingID,
        ConstructionTypeRef,
        ScopeID,
        LifetimeRegionID,
        AccessMode,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    auto add_capture(
        ProgramSpellingID,
        ConstructionTypeRef,
        ScopeID,
        LifetimeRegionID,
        CaptureMode,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    auto add_owner_binding(
        ProgramSpellingID,
        ConstructionTypeRef,
        ScopeID,
        LifetimeRegionID,
        bool,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    auto add_pattern(ElaboratedPattern) noexcept -> PatternID;
    auto pattern_copy(PatternID) const noexcept -> ElaboratedPattern;
    auto place_lifetime(PlaceHandle) const noexcept -> LifetimeRegionID;
    auto place_access(PlaceHandle) const noexcept -> AccessMode;
    auto append_value(
        ConstructionTypeRef,
        LifetimeRegionID,
        expression_construction::Input,
        ProgramOriginID
    ) noexcept -> ExpressionHandle;
    auto append_place(
        PlaceHandle,
        ConstructionTypeRef,
        expression_construction::Projection,
        ProgramOriginID
    ) noexcept -> PlaceHandle;
    auto append_cpp_place(
        PlaceHandle source,
        ConstructionTypeRef type,
        CppOperation operation,
        std::vector<SemCallArgument<ConstructionTypeRef, FailureTermID>> operands,
        ProgramOriginID origin
    ) noexcept -> PlaceHandle;
    auto append_unresolved_callable_borrow(
        ConstructionTypeRef,
        LifetimeRegionID,
        UnresolvedCallableBorrowOp,
        ProgramOriginID
    ) noexcept -> ExpressionHandle;
    auto add_expression(DraftExpression) noexcept -> ExpressionHandle;
    auto take_value(ExpressionHandle) noexcept -> DraftExpression;
    auto take_place(PlaceHandle) noexcept -> DraftExpression;
    auto callable_expression(CallableID, ProgramOriginID) noexcept -> DraftExpression;
    auto call_expression(
        expression_construction::Callee,
        const std::vector<expression_construction::Argument>&,
        ConstructionTypeRef,
        FailureTermID,
        ProgramOriginID
    ) noexcept -> DraftExpression;
    auto make_expression(
        ConstructionTypeRef,
        LifetimeRegionID,
        ProgramOriginID,
        DraftExpressionValue,
        std::optional<ConstantID> = std::nullopt
    ) noexcept -> DraftExpression;
    auto finish(DraftRegion) && noexcept -> StructuredBodyDraft;

private:
    BodyBuilder(BodyReservation::Consumed, ProgramDraft&) noexcept;
    auto add_binding(
        ProgramSpellingID,
        ConstructionTypeRef,
        ScopeID,
        LifetimeRegionID,
        BindingStorage,
        ProgramOriginID
    ) noexcept -> BoundStorage;
    struct ProjectedPlace final {
        LocalBindingID root;
        DraftExpression expression;
    };
    struct ConsumedPlace final {};
    using PlaceConstruction = std::variant<LocalBindingID, ProjectedPlace, ConsumedPlace>;
    auto add_place(PlaceConstruction) noexcept -> PlaceHandle;
    auto place_root(PlaceHandle) const noexcept -> LocalBindingID;
    auto root_expression(LocalBindingID) noexcept -> DraftExpression;
    ProgramDraft& draft;
    BodyIdentity body_identity;
    BodyID body_id;
    BodyKind body_kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs body_inputs;
    MutableBodyTable<Scope, ScopeID> scopes;
    MutableBodyTable<LifetimeRegion, LifetimeRegionID> lifetime_regions;
    MutableBodyTable<ElaboratedLocalBinding, LocalBindingID> bindings;
    MutableBodyTable<ElaboratedPattern, PatternID> patterns;
    std::vector<std::optional<DraftExpression>> values;
    std::vector<PlaceConstruction> places;
    std::optional<LifetimeRegionID> active_lifetime;
};
