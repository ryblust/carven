module carven:semantic.analysis.failure;

import :semantic.analysis.diagnostics;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.provenance;
import :source.provenance.ids;
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

struct FailureTerm final {
    std::vector<TypeID> direct_members;
    std::vector<FailureTermID> inputs;
    struct GuardedContribution final {
        FailureTermID gate;
        FailureTermID source;

        auto operator==(const GuardedContribution&) const noexcept -> bool = default;
    };
    std::vector<GuardedContribution> guarded_inputs;
    std::vector<TypeID> excluded_members;
    std::optional<std::vector<TypeID>> retained_members;
};

struct RequiresEmptyFailure final {
    FailureTermID term;
    ProgramOriginID origin;
    EmptyFailureRequirementKind kind;
};

struct RequiresNonEmptyFailure final {
    FailureTermID term;
    ProgramOriginID origin;
};

struct RequiresFailureSubset final {
    FailureTermID actual;
    FailureTermID allowed;
    ProgramOriginID origin;
    FailureSubsetRequirementKind kind;
};

struct RequiresEqualFailures final {
    FailureTermID left;
    FailureTermID right;
    ProgramOriginID origin;
};

struct RequiresDeclaredFailureContract final {
    FailureTermID actual;
    ProgramOriginID origin;
};

using FailureConstraint = std::variant<
    RequiresEmptyFailure,
    RequiresNonEmptyFailure,
    RequiresFailureSubset,
    RequiresEqualFailures,
    RequiresDeclaredFailureContract>;

class FrozenFailureConstraints final {
public:
    FrozenFailureConstraints(const FrozenFailureConstraints&) = delete;
    FrozenFailureConstraints(FrozenFailureConstraints&&) = default;
    auto operator=(const FrozenFailureConstraints&) -> FrozenFailureConstraints& = delete;
    auto operator=(FrozenFailureConstraints&&) -> FrozenFailureConstraints& = default;
    ~FrozenFailureConstraints() = default;

    auto owner() const noexcept -> ProgramIdentity;
    auto provenance_owner() const noexcept -> ProvenanceIdentity;
    auto terms() const noexcept -> const ImmutableProgramTable<FailureTerm, FailureTermID>&;
    auto constraints() const noexcept -> std::span<const FailureConstraint>;

private:
    FrozenFailureConstraints(
        ImmutableProgramTable<FailureTerm, FailureTermID> terms,
        std::vector<FailureConstraint> constraints,
        ProvenanceIdentity provenance
    ) noexcept;

    ImmutableProgramTable<FailureTerm, FailureTermID> term_table;
    std::vector<FailureConstraint> requirements;
    ProvenanceIdentity provenance_identity;

    friend class FailureConstraintStore;
};

class FailureConstraintStore final {
public:
    FailureConstraintStore(ProgramIdentity owner, ProvenanceIdentity provenance) noexcept;
    FailureConstraintStore(const FailureConstraintStore&) = delete;
    FailureConstraintStore(FailureConstraintStore&&) = default;
    auto operator=(const FailureConstraintStore&) -> FailureConstraintStore& = delete;
    auto operator=(FailureConstraintStore&&) -> FailureConstraintStore& = default;
    ~FailureConstraintStore() = default;

    auto add_empty_term() noexcept -> FailureTermID;
    auto add_concrete_term(std::vector<TypeID> members) noexcept -> FailureTermID;
    auto add_union_term(std::vector<FailureTermID> inputs) noexcept -> FailureTermID;
    auto add_residual_term(FailureTermID input, std::vector<TypeID> handled_members) noexcept
        -> FailureTermID;
    auto add_intersection_term(FailureTermID input, std::vector<TypeID> retained_members) noexcept
        -> FailureTermID;
    auto copy(FailureTermID term) const noexcept -> FailureTerm;
    auto add_member(FailureTermID destination, TypeID member) noexcept -> void;
    auto add_contribution(FailureTermID destination, FailureTermID source) noexcept -> void;
    auto add_guarded_contribution(
        FailureTermID destination,
        FailureTermID gate,
        FailureTermID source
    ) noexcept -> void;
    auto equate(FailureTermID left, FailureTermID right) noexcept -> void;

    auto require_empty(
        FailureTermID term,
        ProgramOriginID origin,
        EmptyFailureRequirementKind kind
    ) noexcept -> void;
    auto require_non_empty(FailureTermID term, ProgramOriginID origin) noexcept -> void;
    auto require_subset(
        FailureTermID actual,
        FailureTermID allowed,
        ProgramOriginID origin,
        FailureSubsetRequirementKind kind
    ) noexcept -> void;
    auto require_equal(FailureTermID left, FailureTermID right, ProgramOriginID origin) noexcept
        -> void;
    auto require_declared_contract(FailureTermID actual, ProgramOriginID origin) noexcept -> void;

    auto finish() && noexcept -> FrozenFailureConstraints;

private:
    auto require_term(FailureTermID term) const noexcept -> void;
    auto require_origin(ProgramOriginID origin) const noexcept -> void;
    auto normalize_members(std::vector<TypeID> members) const noexcept -> std::vector<TypeID>;
    auto normalize_inputs(std::vector<FailureTermID> inputs) const noexcept
        -> std::vector<FailureTermID>;

    ProgramIdentity program_identity;
    ProvenanceIdentity provenance_identity;
    MutableProgramTable<FailureTerm, FailureTermID> term_table;
    std::vector<FailureConstraint> requirements;
};

class FailureSolution final {
public:
    FailureSolution(const FailureSolution&) = delete;
    FailureSolution(FailureSolution&&) = default;
    auto operator=(const FailureSolution&) -> FailureSolution& = delete;
    auto operator=(FailureSolution&&) -> FailureSolution& = default;
    ~FailureSolution() = default;

    auto owner() const noexcept -> ProgramIdentity;
    auto contains(FailureTermID term) const noexcept -> bool;
    auto failure_set(FailureTermID term) const noexcept -> FailureSetID;

private:
    FailureSolution(ProgramIdentity identity, std::vector<FailureSetID> solutions) noexcept;

    ProgramIdentity program_identity;
    std::vector<FailureSetID> failure_sets_by_term;

    friend auto solve_failure_constraints(
        FrozenFailureConstraints&&,
        FailureSetStoreBuilder&,
        CompilationProvenanceReader,
        AnalysisDiagnostics
    ) noexcept -> AnalysisResult<FailureSolution>;
};

auto solve_failure_constraints(
    FrozenFailureConstraints&& constraints,
    FailureSetStoreBuilder& failure_sets,
    CompilationProvenanceReader provenance,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<FailureSolution>;
