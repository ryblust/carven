module carven:semantic.analysis.model;

import :semantic.analysis.failure;
import :semantic.analysis.generics.snapshot;
import :semantic.semir.contents;
import :semantic.semir.callable;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

// Definition bodies and executable bodies share flow and local identities without
// introducing pretend canonical types or executable template declarations.
using AnalysisBodyID = std::variant<BodyID, GenericDeclarationID>;

struct AnalysisSymbolicFailure final {
    ConstructionFailureRef source;
    GenericFailureMask mask;

    auto operator<=>(const AnalysisSymbolicFailure&) const noexcept = default;
};

struct AnalysisFailureKey final {
    std::variant<TypeID, AnalysisSymbolicFailure> value;

    explicit AnalysisFailureKey(TypeID type) noexcept;
    explicit AnalysisFailureKey(AnalysisSymbolicFailure failure) noexcept;
    auto operator<=>(const AnalysisFailureKey&) const noexcept = default;
};

struct AnalysisPointerType final {
    ConstructionTypeRef target;
    PointerAccess access;
};

using AnalysisTypeShape = std::variant<
    BuiltinTypeValue,
    StructTypeValue,
    EnumTypeValue,
    ConstructionArrayTypeValue,
    ConstructionTupleTypeValue,
    ConstructionSliceTypeValue,
    GenericVectorType,
    RangeTypeValue,
    AnalysisPointerType,
    FunctionTypeValue,
    ClosureTypeValue,
    ConstructionCallableViewTypeValue,
    ConstructionOperationTypeValue,
    CppTypeValue,
    GenericTypeParameter,
    GenericNominalApplication,
    GenericCppNamedType,
    GenericCppQueryType>;

class AnalysisBodyView final {
public:
    explicit AnalysisBodyView(const SemIRBody& body) noexcept;
    explicit AnalysisBodyView(const StructuredBodyDraft& body) noexcept;
    auto id() const noexcept -> AnalysisBodyID;
    auto inputs() const noexcept -> const BodyInputs&;
    auto lifetime_regions() const noexcept -> const LifetimeRegionTree&;

    auto bindings() const noexcept
        -> std::span<const std::pair<LocalBindingID, ElaboratedLocalBinding>>;

    auto binding(LocalBindingID id) const noexcept -> const ElaboratedLocalBinding&;
    auto pattern(PatternID id) const noexcept -> const ElaboratedPattern&;

    auto elaborated_pattern_table() const noexcept
        -> const ImmutableBodyTable<ElaboratedPattern, PatternID>&;

    auto pattern_table() const noexcept -> const ImmutableBodyTable<Pattern, PatternID>&;
    auto region() const noexcept -> const SemanticRegion&;
    auto specialized() const noexcept -> std::optional<BodyID>;
    auto concrete() const noexcept -> const SemIRBody*;

private:
    std::variant<const SemIRBody*, const StructuredBodyDraft*> source;
    std::vector<std::pair<LocalBindingID, ElaboratedLocalBinding>> binding_rows;
    std::vector<ElaboratedPattern> pattern_rows;
};

class AnalysisModelView final {
public:
    AnalysisModelView(
        const SemIRProgram& program,
        const GenericAnalysisSnapshot& generics
    ) noexcept;

    auto bodies() const noexcept -> const std::map<AnalysisBodyID, AnalysisBodyView>&;
    auto body(AnalysisBodyID id) const noexcept -> const AnalysisBodyView&;
    auto shape(ConstructionTypeRef type) const noexcept -> const AnalysisTypeShape&;
    auto concrete_type(ConstructionTypeRef type) const noexcept -> std::optional<TypeID>;
    auto callable_identity(ConstructionTypeRef type) const noexcept -> std::optional<CallableID>;
    auto contents(ConstructionTypeRef type) const noexcept -> TypeContents;
    auto callable_adaptation(ConstructionTypeRef from, ConstructionTypeRef to) const noexcept
        -> CallableAdaptation;
    auto fields(GenericTypeID type) const noexcept -> std::vector<ConstructionTypeRef>;
    auto enum_case_count(ConstructionEnumCaseRef reference) const noexcept -> std::size_t;
    auto failure_support(ConstructionFailureRef failures) const noexcept -> GenericFailureMask;

    auto failure_known_members(ConstructionFailureRef failures) const noexcept
        -> std::vector<TypeID>;

    auto failure_may_contain(ConstructionFailureRef failures, TypeID member) const noexcept -> bool;

    auto failure_closed_set(ConstructionFailureRef failures) const noexcept
        -> std::optional<FailureSetID>;

    const SemIRProgram& program;
    const GenericAnalysisSnapshot& generics;

private:
    auto compute_shape(ConstructionTypeRef type) const noexcept -> AnalysisTypeShape;
    std::map<AnalysisBodyID, AnalysisBodyView> body_views;
    mutable std::map<ConstructionTypeRef, AnalysisTypeShape> shapes;
    mutable std::map<ConstructionTypeRef, TypeContents> content_facts;
};
