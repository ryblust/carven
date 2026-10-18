module carven:semantic.analysis.failure;

import :semantic.analysis.diagnostics;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.provenance;
import std;

enum class EmptyFailureRequirementKind {
    OrdinaryConsumption,
    CatchResidual,
    RootBoundary,
};

enum class FailureSubsetRequirementKind {
    DeclaredCallable,
    CallableAdoption,
};

struct GenericFailureParameter final {
    GenericDeclarationID definition;
    std::uint32_t index;

    auto operator<=>(const GenericFailureParameter&) const noexcept = default;
};

// A complemented mask denotes every nominal member except the listed members.
// Its meaning does not depend on the nominal types declared in this program.
struct GenericFailureMask final {
    bool complemented;
    std::vector<TypeID> members;

    auto operator<=>(const GenericFailureMask&) const noexcept = default;
};

auto intersect_failure_masks(
    const GenericFailureMask& left,
    const GenericFailureMask& right
) noexcept -> GenericFailureMask;
auto residual_failure_mask(const GenericFailureMask& source, std::vector<TypeID> excluded) noexcept
    -> GenericFailureMask;
auto failure_mask_contains(const GenericFailureMask& mask, TypeID member) noexcept -> bool;

struct GenericFailureComponent final {
    GenericFailureParameter parameter;
    GenericFailureMask mask;

    auto operator==(const GenericFailureComponent&) const noexcept -> bool = default;
};

struct GenericFailureGuard final {
    GenericFailureID gate;
    GenericFailureID source;

    auto operator==(const GenericFailureGuard&) const noexcept -> bool = default;
};

struct GenericFailureExpression final {
    std::vector<TypeID> concrete_members;
    std::vector<GenericFailureComponent> parameters;
    std::vector<GenericFailureGuard> guards;

    auto operator==(const GenericFailureExpression&) const noexcept -> bool = default;
};

enum class FailureProof {
    Proven,
    Disproven,
    Unknown,
};

class GenericFailureAlgebra final {
public:
    explicit GenericFailureAlgebra(ProgramIdentity owner) noexcept;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(GenericFailureID id) const noexcept -> bool;
    auto copy(GenericFailureID id) const noexcept -> GenericFailureExpression;
    auto intern(GenericFailureExpression expression) noexcept -> GenericFailureID;
    auto unite(std::span<const GenericFailureID> inputs) noexcept -> GenericFailureID;
    auto residual(GenericFailureID input, std::vector<TypeID> members) noexcept -> GenericFailureID;

    auto intersection(GenericFailureID input, std::vector<TypeID> members) noexcept
        -> GenericFailureID;

    auto guard(GenericFailureID gate, GenericFailureID source) noexcept -> GenericFailureID;
    // Subset quantifies over all parameter environments; Disproven denotes a
    // counterexample. Emptiness queries instead return definite truth: a
    // parameter-dependent set can be neither definitely empty nor nonempty.
    auto subset(GenericFailureID actual, GenericFailureID allowed) const noexcept -> FailureProof;
    auto empty(GenericFailureID source) const noexcept -> FailureProof;
    auto non_empty(GenericFailureID source) const noexcept -> FailureProof;
    auto support(GenericFailureID source) const noexcept -> GenericFailureMask;
    auto known_members(GenericFailureID source) const noexcept -> std::vector<TypeID>;
    auto lower_bound(GenericFailureID source) noexcept -> GenericFailureID;
    auto upper_bound(GenericFailureID source) noexcept -> GenericFailureID;
    auto ids() const noexcept -> std::vector<GenericFailureID>;

private:
    friend class SemanticArtifactAccess;
    auto normalize_members(std::vector<TypeID> members) const noexcept -> std::vector<TypeID>;
    auto bounds(GenericFailureID source, bool upper) const noexcept -> GenericFailureExpression;
    auto filter(GenericFailureID input, GenericFailureMask mask) noexcept -> GenericFailureID;

    MutableProgramTable<GenericFailureExpression, GenericFailureID> expressions;
};

struct FailureTerm final {
    std::vector<TypeID> direct_members;
    std::vector<ConstructionFailureRef> inputs;

    struct GuardedContribution final {
        ConstructionFailureRef gate;
        ConstructionFailureRef source;

        auto operator==(const GuardedContribution&) const noexcept -> bool = default;
    };

    std::vector<GuardedContribution> guarded_inputs;
    std::vector<TypeID> excluded_members;
    std::optional<std::vector<TypeID>> retained_members;

    // A throw statement that introduces a direct member; diagnostics only.
    struct ThrowSite final {
        TypeID member;
        ProgramOriginID origin;
    };

    std::vector<ThrowSite> throw_sites;
};

struct RequiresEmptyFailure final {
    ConstructionFailureRef source;
    ProgramOriginID origin;
    EmptyFailureRequirementKind kind;
};

struct RequiresNonEmptyFailure final {
    ConstructionFailureRef source;
    ProgramOriginID origin;
    bool allow_dependent;
};

struct RequiresFailureSubset final {
    ConstructionFailureRef actual;
    ConstructionFailureRef allowed;
    ProgramOriginID origin;
    FailureSubsetRequirementKind kind;
};

struct RequiresEqualFailures final {
    ConstructionFailureRef left;
    ConstructionFailureRef right;
    ProgramOriginID origin;
};

struct RequiresDeclaredFailureContract final {
    ConstructionFailureRef actual;
    ProgramOriginID origin;
};

using FailureConstraint = std::variant<
    RequiresEmptyFailure,
    RequiresNonEmptyFailure,
    RequiresFailureSubset,
    RequiresEqualFailures,
    RequiresDeclaredFailureContract>;

class FailureTypeDiagnosticNames;
class FailureSolution;

class FrozenFailureConstraints final {
public:
    FrozenFailureConstraints(const FrozenFailureConstraints&) = delete;
    FrozenFailureConstraints(FrozenFailureConstraints&&) = default;
    auto operator=(const FrozenFailureConstraints&) -> FrozenFailureConstraints& = delete;
    auto operator=(FrozenFailureConstraints&&) -> FrozenFailureConstraints& = delete;
    ~FrozenFailureConstraints() = default;
    auto owner() const noexcept -> ProgramIdentity;
    auto provenance_owner() const noexcept -> ProvenanceIdentity;
    auto terms() const noexcept -> const ImmutableProgramTable<FailureTerm, FailureTermID>&;
    auto constraints() const noexcept -> std::span<const FailureConstraint>;
    auto generic_failures() const noexcept -> const GenericFailureAlgebra&;

private:
    FrozenFailureConstraints(
        ImmutableProgramTable<FailureTerm, FailureTermID> terms,
        std::vector<FailureConstraint> constraints,
        ProvenanceIdentity provenance,
        std::shared_ptr<GenericFailureAlgebra> generic_failures,
        std::vector<FailureTermID> pending_applications
    ) noexcept;

    ImmutableProgramTable<FailureTerm, FailureTermID> term_table;
    std::vector<FailureConstraint> requirements;
    ProvenanceIdentity provenance_identity;
    std::shared_ptr<GenericFailureAlgebra> symbolic_graph;
    std::vector<FailureTermID> pending_applications;

    friend class FailureConstraintStore;
    friend auto solve_failure_constraints(
        FrozenFailureConstraints&&,
        FailureSetStoreBuilder&,
        CompilationProvenanceReader,
        const FailureTypeDiagnosticNames&,
        AnalysisDiagnostics,
        bool
    ) noexcept -> AnalysisResult<FailureSolution>;
};

using FailureParameterEnvironment = std::map<GenericFailureParameter, ConstructionFailureRef>;

struct FailureApplication final {
    FailureTermID source;
    FailureParameterEnvironment environment;
    FailureTermID result;
    bool complete;
    std::optional<std::size_t> parent;
};

class FailureConstraintStore final {
public:
    FailureConstraintStore(ProgramIdentity owner, ProvenanceIdentity provenance) noexcept;
    FailureConstraintStore(const FailureConstraintStore&) = delete;
    FailureConstraintStore(FailureConstraintStore&&) = default;
    auto operator=(const FailureConstraintStore&) -> FailureConstraintStore& = delete;
    auto operator=(FailureConstraintStore&&) -> FailureConstraintStore& = delete;
    ~FailureConstraintStore() = default;
    auto add_generic_parameter(GenericFailureParameter parameter) noexcept -> GenericFailureID;
    auto intern_generic_failure(GenericFailureExpression expression) noexcept -> GenericFailureID;
    auto generic_failure_copy(GenericFailureID id) const noexcept -> GenericFailureExpression;
    auto add_generic_concrete(std::vector<TypeID> members) noexcept -> GenericFailureID;
    auto add_generic_union(std::vector<GenericFailureID> inputs) noexcept -> GenericFailureID;

    auto add_generic_residual(GenericFailureID input, std::vector<TypeID> members) noexcept
        -> GenericFailureID;

    auto add_generic_intersection(GenericFailureID input, std::vector<TypeID> members) noexcept
        -> GenericFailureID;

    auto add_generic_guard(GenericFailureID gate, GenericFailureID source) noexcept
        -> GenericFailureID;

    auto generic_subset(GenericFailureID actual, GenericFailureID allowed) const noexcept
        -> FailureProof;

    auto generic_empty(GenericFailureID source) const noexcept -> FailureProof;
    auto generic_non_empty(GenericFailureID source) const noexcept -> FailureProof;
    auto add_empty_term() noexcept -> FailureTermID;
    auto add_concrete_term(std::vector<TypeID> members) noexcept -> FailureTermID;
    auto add_union_term(std::vector<ConstructionFailureRef> inputs) noexcept -> FailureTermID;

    auto add_residual_term(
        ConstructionFailureRef input,
        std::vector<TypeID> handled_members
    ) noexcept -> FailureTermID;

    auto add_intersection_term(
        ConstructionFailureRef input,
        std::vector<TypeID> retained_members
    ) noexcept -> FailureTermID;

    auto copy(FailureTermID term) const noexcept -> FailureTerm;
    auto add_member(FailureTermID destination, TypeID member) noexcept -> void;

    auto add_thrown_member(
        FailureTermID destination,
        TypeID member,
        ProgramOriginID origin
    ) noexcept -> void;

    auto add_contribution(FailureTermID destination, ConstructionFailureRef source) noexcept
        -> void;

    auto add_guarded_contribution(
        FailureTermID destination,
        ConstructionFailureRef gate,
        ConstructionFailureRef source
    ) noexcept -> void;

    auto equate(FailureTermID left, FailureTermID right) noexcept -> void;

    auto require_empty(
        ConstructionFailureRef source,
        ProgramOriginID origin,
        EmptyFailureRequirementKind kind
    ) noexcept -> void;

    auto require_non_empty(
        ConstructionFailureRef source,
        ProgramOriginID origin,
        bool allow_dependent = false
    ) noexcept -> void;

    auto require_subset(
        ConstructionFailureRef actual,
        ConstructionFailureRef allowed,
        ProgramOriginID origin,
        FailureSubsetRequirementKind kind
    ) noexcept -> void;

    auto require_equal(
        ConstructionFailureRef left,
        ConstructionFailureRef right,
        ProgramOriginID origin
    ) noexcept -> void;

    auto require_declared_contract(ConstructionFailureRef actual, ProgramOriginID origin) noexcept
        -> void;

    auto complete_applications(
        FailureSetStoreBuilder& sets,
        std::span<const ConstructionFailureRef> roots = {}
    ) noexcept -> bool;

    auto growing_parameter() const noexcept -> std::optional<GenericFailureParameter>;

    auto dependencies(ConstructionFailureRef source) const noexcept
        -> std::vector<ConstructionFailureRef>;

    auto application(
        FailureTermID source,
        FailureParameterEnvironment environment,
        FailureSetStoreBuilder& sets
    ) noexcept -> FailureTermID;

    auto parameter(GenericFailureParameter source) noexcept -> GenericFailureID;
    auto finish() && noexcept -> FrozenFailureConstraints;
    auto snapshot() const noexcept -> FrozenFailureConstraints;

private:
    friend class SemanticArtifactAccess;
    auto require_term(FailureTermID term) const noexcept -> void;
    auto require_source(ConstructionFailureRef source) const noexcept -> void;
    auto require_origin(ProgramOriginID origin) const noexcept -> void;
    auto normalize_members(std::vector<TypeID> members) const noexcept -> std::vector<TypeID>;

    auto normalize_inputs(std::vector<ConstructionFailureRef> inputs) const noexcept
        -> std::vector<ConstructionFailureRef>;

    auto reduce_application_argument(
        ConstructionFailureRef source,
        FailureSetStoreBuilder& sets
    ) noexcept -> ConstructionFailureRef;

    auto normalize_application_environment(
        FailureParameterEnvironment environment,
        FailureSetStoreBuilder& sets
    ) noexcept -> FailureParameterEnvironment;

    ProgramIdentity program_identity;
    ProvenanceIdentity provenance_identity;
    MutableProgramTable<FailureTerm, FailureTermID> term_table;
    std::vector<FailureConstraint> requirements;
    std::shared_ptr<GenericFailureAlgebra> symbolic_graph;

    std::map<std::pair<FailureTermID, FailureParameterEnvironment>, FailureTermID>
        application_cache;
    std::vector<FailureApplication> applications;
    std::map<FailureTermID, std::size_t> applications_by_result;
    std::optional<std::size_t> active_application;
    std::optional<GenericFailureParameter> growth;
    bool closing_applications = false;

    friend class FailureInstantiation;
};

struct FailureInstantiationEnvironment final {
    std::function<ConstructionFailureRef(GenericFailureParameter)> parameter;
    std::function<TypeID(TypeID)> member;
    std::function<ProgramOriginID(ProgramOriginID)> origin;
};

class FailureInstantiation final {
public:
    FailureInstantiation(
        FailureConstraintStore& destination,
        FailureSetStoreBuilder& failure_sets,
        FailureInstantiationEnvironment environment,
        std::optional<FailureParameterEnvironment> deferred = std::nullopt
    ) noexcept;

    auto substitute(ConstructionFailureRef source) noexcept -> ConstructionFailureRef;

private:
    auto substitute_term(FailureTermID source) noexcept -> ConstructionFailureRef;
    auto populate_term(FailureTermID source, FailureTermID destination) noexcept -> void;
    auto substitute_generic(GenericFailureID source) noexcept -> ConstructionFailureRef;
    auto independent(ConstructionFailureRef source) noexcept -> bool;

    FailureConstraintStore& destination;
    FailureSetStoreBuilder& failure_sets;
    FailureInstantiationEnvironment environment;
    std::map<ConstructionFailureRef, ConstructionFailureRef> substituted;
    std::map<ConstructionFailureRef, bool> independent_sources;
    std::optional<FailureParameterEnvironment> deferred_parameters;

    friend class FailureConstraintStore;
};

class FailureTypeDiagnosticNames final {
public:
    FailureTypeDiagnosticNames(
        const CanonicalTypeStoreBuilder& types,
        DeclarationConstructionView declarations,
        CompilationProvenanceReader provenance
    ) noexcept;

    auto name(TypeID type) const noexcept -> std::string;

private:
    const CanonicalTypeStoreBuilder& types;
    DeclarationConstructionView declarations;
    CompilationProvenanceReader provenance;
};

class FailureSolution final {
public:
    FailureSolution(const FailureSolution&) = delete;
    FailureSolution(FailureSolution&&) = default;
    auto operator=(const FailureSolution&) -> FailureSolution& = delete;
    auto operator=(FailureSolution&&) -> FailureSolution& = delete;
    ~FailureSolution() = default;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(FailureTermID term) const noexcept -> bool;
    auto failure_set(ConstructionFailureRef source) const noexcept -> FailureSetID;
    auto normalize(ConstructionFailureRef source) const noexcept -> ConstructionFailureRef;
    auto closed_set(ConstructionFailureRef source) const noexcept -> std::optional<FailureSetID>;
    auto generic_failures() const noexcept -> const GenericFailureAlgebra&;
    auto term_copy(FailureTermID term) const noexcept -> FailureTerm;
    auto conditional_non_empty() const noexcept -> std::span<const RequiresNonEmptyFailure>;
    auto support(ConstructionFailureRef source) const noexcept -> GenericFailureMask;
    auto known_members(ConstructionFailureRef source) const noexcept -> std::vector<TypeID>;

private:
    FailureSolution(
        ProgramIdentity identity,
        std::vector<ConstructionFailureRef> solutions,
        std::shared_ptr<GenericFailureAlgebra> generic_failures,
        ImmutableProgramTable<FailureTerm, FailureTermID> terms,
        std::map<GenericFailureID, FailureSetID> closed_generic_sets,
        std::vector<RequiresNonEmptyFailure> conditional_non_empty,
        std::vector<GenericFailureID> upper_bounds,
        std::vector<std::vector<TypeID>> known_members,
        FailureSetStore failure_sets,
        std::vector<bool> pending_terms
    ) noexcept;

    ProgramIdentity program_identity;
    std::vector<ConstructionFailureRef> failures_by_term;
    std::shared_ptr<GenericFailureAlgebra> symbolic_graph;
    ImmutableProgramTable<FailureTerm, FailureTermID> term_graph;
    std::map<GenericFailureID, FailureSetID> closed_generic_sets;
    std::vector<RequiresNonEmptyFailure> conditional_propagations;
    std::vector<GenericFailureID> upper_bounds_by_term;
    std::vector<std::vector<TypeID>> known_members_by_term;
    FailureSetStore known_sets;
    std::vector<bool> pending_by_term;

    friend auto solve_failure_constraints(
        FrozenFailureConstraints&&,
        FailureSetStoreBuilder&,
        CompilationProvenanceReader,
        const FailureTypeDiagnosticNames&,
        AnalysisDiagnostics,
        bool
    ) noexcept -> AnalysisResult<FailureSolution>;
};

auto solve_failure_constraints(
    FrozenFailureConstraints&& constraints,
    FailureSetStoreBuilder& failure_sets,
    CompilationProvenanceReader provenance,
    const FailureTypeDiagnosticNames& type_names,
    AnalysisDiagnostics diagnostics,
    bool validate_requirements = true
) noexcept -> AnalysisResult<FailureSolution>;
