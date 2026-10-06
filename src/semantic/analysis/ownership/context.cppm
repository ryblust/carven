module carven:semantic.analysis.ownership.context;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.ownership;
import :semantic.semir.contents;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.task;
import :support.visit;
import std;

// A missing component denotes an unknown array element. Paths describe storage,
// not the computations that selected it.
using OwnershipProjectionPath = std::vector<std::optional<std::uint64_t>>;

// Sequence storage is indirect. These checked selection boundaries are cut
// into referent roots at a call boundary; inline paths remain finite.
struct OwnershipIndirection final {
    std::size_t offset;
    TypeID element;
    ProgramOriginID site;

    auto operator<=>(const OwnershipIndirection& other) const noexcept {
        return std::tie(offset, element) <=> std::tie(other.offset, other.element);
    }

    auto operator==(const OwnershipIndirection& other) const noexcept -> bool {
        return (*this <=> other) == 0;
    }
};

struct OwnershipPlace final {
    std::size_t object;
    OwnershipProjectionPath path;
    std::vector<OwnershipIndirection> indirections {};
    auto operator<=>(const OwnershipPlace&) const noexcept = default;
};

struct OwnershipCallableLoan final {
    OwnershipProjectionPath holder;
    std::optional<OwnershipPlace> backing;
    std::optional<CallableID> callable;
    ProgramOriginID origin;
    bool direct_only;

    auto operator<=>(const OwnershipCallableLoan& other) const noexcept -> std::strong_ordering;
    auto operator==(const OwnershipCallableLoan& other) const noexcept -> bool;
};

struct OwnershipCapture final {
    OwnershipProjectionPath holder;
    OwnershipPlace target;
    ProgramOriginID origin;

    auto operator<=>(const OwnershipCapture& other) const noexcept -> std::strong_ordering;
    auto operator==(const OwnershipCapture& other) const noexcept -> bool;
};

// Only known Carven backing creates a storage loan. An empty set makes no claim
// about native storage lifetime. Origins do not participate in solver identity.
struct OwnershipStorageLoan final {
    OwnershipProjectionPath holder;
    OwnershipPlace backing;
    ProgramOriginID origin;

    auto operator<=>(const OwnershipStorageLoan& other) const noexcept -> std::strong_ordering;
    auto operator==(const OwnershipStorageLoan& other) const noexcept -> bool;
};

struct OwnershipRelationshipRows final {
    std::vector<OwnershipCallableLoan> callable_loans;
    std::vector<OwnershipCapture> captures;
    std::vector<OwnershipStorageLoan> storage_loans;
    auto operator==(const OwnershipRelationshipRows&) const noexcept -> bool = default;
};

// Absent storage represents an empty value, not unknown backing. Mutating,
// assigning, or consuming an owner ends its outstanding row borrows.
class OwnershipRelationships final {
public:
    OwnershipRelationships() = default;
    explicit OwnershipRelationships(OwnershipRelationshipRows rows) noexcept;
    OwnershipRelationships(const OwnershipRelationships& other) noexcept;
    OwnershipRelationships(OwnershipRelationships&&) = default;
    auto operator=(const OwnershipRelationships& other) noexcept -> OwnershipRelationships&;
    auto operator=(OwnershipRelationships&&) -> OwnershipRelationships& = default;
    ~OwnershipRelationships() = default;
    auto view() const noexcept -> const OwnershipRelationshipRows&;
    auto edit_existing() noexcept -> OwnershipRelationshipRows*;
    auto edit() noexcept -> OwnershipRelationshipRows&;
    auto empty() const noexcept -> bool;
    auto operator==(const OwnershipRelationships& other) const noexcept -> bool;

private:
    std::unique_ptr<OwnershipRelationshipRows> rows;
};

struct OwnershipObjectState final {
    bool available;
    std::optional<ProgramOriginID> taken;
    OwnershipRelationships relationships;
    bool modified;

    auto operator==(const OwnershipObjectState& other) const noexcept -> bool;
};

struct OwnershipState final {
    std::vector<OwnershipObjectState> objects;
    auto operator==(const OwnershipState&) const noexcept -> bool = default;
};

struct OwnershipReturn final {
    OwnershipRelationships value;
};

struct OwnershipFailure final {
    TypeID type;
    OwnershipRelationships value;
};

struct OwnershipTestStopped final {};

struct OwnershipBreak final {};

struct OwnershipContinue final {};

using OwnershipExitPayload = std::variant<
    OwnershipReturn,
    OwnershipFailure,
    OwnershipTestStopped,
    OwnershipBreak,
    OwnershipContinue>;

struct OwnershipExit final {
    OwnershipExitPayload payload;
    OwnershipState state;
};

struct OwnershipNormal final {
    OwnershipState state;
    OwnershipRelationships value;
    // Selected storage is expression provenance, not contents copied into a value.
    std::vector<OwnershipPlace> storage;
};

struct OwnershipFlow final {
    std::optional<OwnershipNormal> normal;
    std::vector<OwnershipExit> exits;
};

struct OwnershipCondition final {
    std::optional<OwnershipNormal> yes;
    std::optional<OwnershipNormal> no;
    std::vector<OwnershipExit> exits;
};

enum class OwnershipAccessKind { Active, Stable, Structural };

struct OwnershipAccess final {
    OwnershipPlace place;
    OwnershipAccessKind kind;
    bool descendants = false;
    auto operator<=>(const OwnershipAccess&) const noexcept = default;
};

struct OwnershipCallArgument final {
    std::optional<OwnershipPlace> alias;
    OwnershipRelationships value;
    std::vector<OwnershipPlace> storage;
    std::optional<OwnershipPlace> capture_holder;
    auto operator==(const OwnershipCallArgument&) const noexcept -> bool = default;
};

struct OwnershipStorageSite final {
    BodyID body;
    std::size_t slot;
    bool input;
    std::optional<ProgramOriginID> element_selection {};
    auto operator<=>(const OwnershipStorageSite&) const noexcept = default;
};

struct OwnershipExternalObject final {
    TypeID type;
    ProgramOriginID origin;
    OwnershipObjectState state;
    OwnershipStorageSite site;
    bool many;

    auto operator==(const OwnershipExternalObject& other) const noexcept -> bool;
};

// Carrier ownership is storage topology, not a relationship copied with a value.
struct OwnershipStorageEdge final {
    OwnershipPlace carrier;
    std::size_t element;
    std::optional<std::uint64_t> index;
    bool direct;
    auto operator<=>(const OwnershipStorageEdge&) const noexcept = default;
};

// Call inputs normalize reachable objects and clear modification history.
// Completions retain externally visible writes and backing relationships.
struct OwnershipCallInput final {
    BodyID body_id;
    std::vector<OwnershipCallArgument> parameters;
    std::vector<OwnershipCallArgument> captures;
    std::vector<OwnershipExternalObject> objects;
    std::vector<std::vector<bool>> outlives;
    std::vector<OwnershipAccess> accesses;
    std::vector<OwnershipStorageLoan> storage_readers;
    std::vector<OwnershipStorageEdge> owns {};
    auto operator==(const OwnershipCallInput&) const noexcept -> bool = default;
};

struct OwnershipCallCompletion final {
    bool test_stopped;
    std::optional<TypeID> failure;
    OwnershipState state;
    OwnershipRelationships value;
    auto operator==(const OwnershipCallCompletion&) const noexcept -> bool = default;
};

// Owned first-error fields defer publication without caching source text or a
// diagnosed token. Origin IDs stay valid throughout this immutable program batch.
struct OwnershipRecordedError final {
    DiagnosticCode code;
    std::string message;
    ProgramOriginID origin;
    std::optional<ProgramOriginID> related;
    std::string related_label;
    std::string help;
};

struct OwnershipDiagnosisRecord final {
    std::optional<OwnershipRecordedError> error;
    std::map<ProgramOriginID, bool> returned_copies;
};

struct OwnershipCallQuery final {
    OwnershipCallInput input;
    std::vector<OwnershipCallCompletion> answer;
    std::flat_set<std::size_t> consumers;
    bool queued;
    // The last evaluation's transfer agrees with the accumulated answer.
    bool evaluation_matches_answer;
    std::unique_ptr<OwnershipDiagnosisRecord> diagnosis;
};

struct OwnershipLocalObject final {
    TypeID type;
    ProgramOriginID origin;
    LifetimeRegionID lifetime;
};

struct OwnershipCatchAcceptance final {
    std::vector<std::optional<PatternID>> alternatives;
    bool exhaustive;
};

struct OwnershipBodyFacts final {
    std::vector<OwnershipLocalObject> locals;
    std::flat_map<const SemanticExpression*, std::size_t> temporaries;
    std::flat_map<LifetimeRegionID, std::vector<std::size_t>> lifetime_objects;
    std::flat_map<const SemCatchArm*, std::flat_map<TypeID, OwnershipCatchAcceptance>> catches;
};

struct OwnershipPreparation final {
    std::flat_map<BodyID, OwnershipBodyFacts> body_facts;
    std::flat_map<BodyID, std::uint32_t> recursion_components;
};

// Both results observe the same complete body traversal before the solver runs.
auto prepare_ownership_analysis(const SemIRProgram& program) noexcept -> OwnershipPreparation;

class OwnershipRecursionBuilder final {
public:
    explicit OwnershipRecursionBuilder(const SemIRProgram& program) noexcept;
    // Starts one body before its expressions are observed in preorder.
    auto begin_body(BodyID body) noexcept -> void;
    auto observe(const SemanticExpression& expression) noexcept -> void;
    auto finish() && noexcept -> std::flat_map<BodyID, std::uint32_t>;

private:
    auto body_ordinal(CallableID callable) const noexcept -> std::optional<std::uint32_t>;
    const SemIRProgram& program;
    std::flat_map<BodyID, std::uint32_t> ordinals;
    std::vector<std::vector<std::uint32_t>> adjacency;
    std::uint32_t caller = 0u;
    std::flat_set<const SemanticExpression*> direct_callees;
};

auto select_element_storage(
    const CanonicalTypeStore& types,
    TypeID sequence,
    std::span<const OwnershipPlace> storage,
    const OwnershipRelationships& relationships,
    std::optional<std::uint64_t> index,
    ProgramOriginID selection
) noexcept -> std::vector<OwnershipPlace>;

auto overlaps(
    std::span<const std::optional<std::uint64_t>> left,
    std::span<const std::optional<std::uint64_t>> right
) noexcept -> bool;
auto overlaps(const OwnershipPlace& left, const OwnershipPlace& right) noexcept -> bool;

auto storage_region_ancestor(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& owner,
    const OwnershipPlace& referent,
    bool strict
) noexcept -> bool;
auto storage_regions_overlap(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& left,
    const OwnershipPlace& right
) noexcept -> bool;
auto normalize_storage_loans(std::vector<OwnershipStorageLoan>& loans) noexcept -> void;
auto normalize_relationships(OwnershipRelationships& relationships) noexcept -> void;
auto merge_relationships(
    OwnershipRelationships& destination,
    const OwnershipRelationships& source
) noexcept -> void;
auto project_relationships(
    const OwnershipRelationships& source,
    const OwnershipProjectionPath& path
) noexcept -> OwnershipRelationships;
auto nest_relationships(OwnershipRelationships source, const OwnershipProjectionPath& path) noexcept
    -> OwnershipRelationships;
auto join_ownership_state(OwnershipState& destination, const OwnershipState& source) noexcept
    -> void;
auto join_normal_ownership(
    std::optional<OwnershipNormal>& destination,
    const std::optional<OwnershipNormal>& source
) noexcept -> void;
auto join_normal_ownership(
    std::optional<OwnershipNormal>& destination,
    std::optional<OwnershipNormal>&& source
) noexcept -> void;
auto append_ownership_exits(OwnershipFlow& destination, OwnershipFlow& source) noexcept -> void;
class OwnershipBatchAnalyzer;

struct OwnershipEscape final {
    std::string message;
    ProgramOriginID origin;
    ProgramOriginID related;
};

struct OwnershipBodyResult final {
    std::expected<std::vector<OwnershipCallCompletion>, OwnershipEscape> answer;
    // Events are owned by this evaluation and do not affect its transfer answer.
    std::unique_ptr<OwnershipDiagnosisRecord> diagnosis;
};

class OwnershipBodyAnalyzer final {
public:
    OwnershipBodyAnalyzer(
        OwnershipBatchAnalyzer& analysis,
        const OwnershipCallInput& input
    ) noexcept;
    auto run() noexcept -> OwnershipBodyResult;
    auto check_contracts() noexcept -> std::unique_ptr<OwnershipDiagnosisRecord>;

private:
    auto diagnosis_record() noexcept -> OwnershipDiagnosisRecord&;
    auto diagnose(
        DiagnosticCode code,
        std::string message,
        ProgramOriginID origin,
        std::optional<ProgramOriginID> related = std::nullopt,
        std::string related_label = "related storage or access",
        std::string help = {}
    ) noexcept -> void;
    auto outlives(std::size_t source, std::size_t destination) const noexcept -> bool;
    auto full_expression_storage(std::size_t object) const noexcept -> bool;
    auto leave(OwnershipFlow& flow, LifetimeRegionID lifetime) noexcept -> void;
    auto retain(
        OwnershipState& state,
        const OwnershipRelationships& relationships,
        const SemanticExpression& source
    ) const noexcept -> void;
    auto tracked_borrows(
        const OwnershipRelationships& value,
        const OwnershipState& state
    ) const noexcept -> bool;
    auto use(
        const OwnershipRelationships& relationships,
        const OwnershipState& state,
        ProgramOriginID origin,
        bool direct = false
    ) noexcept -> void;
    auto store(
        OwnershipState& state,
        const OwnershipPlace& target,
        const OwnershipRelationships& relationships,
        ProgramOriginID origin,
        bool definite = true
    ) noexcept -> void;
    // Records whether a returned owner could have been transferred instead of copied.
    auto observe_returned_copy(
        const SemanticExpression& source,
        const OwnershipState& state
    ) noexcept -> void;

    struct TakeConflict final {
        DiagnosticCode code;
        std::string_view message;
        std::optional<ProgramOriginID> related;
    };

    auto take_conflict(const OwnershipState& state, const OwnershipPlace& target) const noexcept
        -> std::optional<TakeConflict>;
    auto storage_write_conflict(
        const OwnershipState& state,
        const OwnershipPlace& target
    ) const noexcept -> std::optional<ProgramOriginID>;
    auto check_storage_write(
        const OwnershipState& state,
        const OwnershipPlace& target,
        ProgramOriginID origin
    ) noexcept -> void;
    auto protect_storage(const OwnershipRelationships& value) noexcept -> void;
    auto restore_storage_readers(std::size_t count) noexcept -> void;
    auto references(
        const OwnershipRelationships& relationships,
        const OwnershipState& state
    ) const noexcept -> std::vector<OwnershipCapture>;
    auto binding_places(LocalBindingID binding, const OwnershipState& state) const noexcept
        -> std::vector<OwnershipPlace>;
    auto binding_place(LocalBindingID binding) const noexcept -> OwnershipPlace;
    auto is_writable(LocalBindingID binding) const noexcept -> bool;
    auto storage_ancestor(
        const OwnershipPlace& owner,
        const OwnershipPlace& referent,
        bool strict
    ) const noexcept -> bool;
    auto storage_overlaps(const OwnershipPlace& left, const OwnershipPlace& right) const noexcept
        -> bool;
    auto write_access(
        const OwnershipPlace& target,
        ProgramOriginID origin,
        bool invalidates = true
    ) noexcept -> void;
    auto require_available(
        const OwnershipState& state,
        const OwnershipPlace& place,
        ProgramOriginID origin
    ) noexcept -> void;
    auto selected_access_kind(const SemanticExpression& source) const noexcept
        -> OwnershipAccessKind;
    auto constant_index(const SemanticExpression& source) const noexcept
        -> std::optional<std::uint64_t>;
    auto complete_place(
        const SemanticExpression& source,
        OwnershipNormal& normal,
        bool read
    ) noexcept -> void;
    auto place(const SemanticExpression& source, OwnershipState state, bool read = true) noexcept
        -> ContinuationTask<OwnershipFlow>;
    static auto is_leaf_expression(const SemanticExpression& source) noexcept -> bool;
    auto finish_expression(
        const SemanticExpression& source,
        OwnershipNormal& normal,
        bool direct
    ) noexcept -> void;
    auto complete_leaf_expression(
        const SemanticExpression& source,
        OwnershipNormal& normal,
        bool direct
    ) noexcept -> void;
    auto expression(
        const SemanticExpression& source,
        OwnershipState state,
        bool direct = false
    ) noexcept -> ContinuationTask<OwnershipFlow>;
    auto complete_expression(const SemanticExpression& source, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto region(const SemanticRegion& source, OwnershipState state, bool release = true) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto statement(const SemanticStatement& source, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto conditional(const SemIf& value, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto match(const SemMatch& value, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto attempt(const SemTry& value, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto loop(const SemLoop& value, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto range(const SemRangeLoop& value, OwnershipState state) noexcept
        -> ContinuationTask<OwnershipFlow>;
    auto call(
        CallableID callable,
        const OwnershipRelationships& captures,
        std::optional<OwnershipPlace> capture_owner,
        std::span<const OwnershipCallArgument> arguments,
        OwnershipState state,
        ProgramOriginID origin
    ) noexcept -> OwnershipFlow;
    auto bind_pattern(
        OwnershipState& state,
        PatternID pattern,
        const OwnershipRelationships& relationships,
        std::span<const OwnershipPlace> places = {}
    ) noexcept -> void;
    auto pattern_condition(
        PatternID pattern,
        std::span<const SemPatternBounds> bounds,
        OwnershipState state
    ) noexcept -> ContinuationTask<OwnershipCondition>;
    auto object_type(std::size_t object) const noexcept -> TypeID;
    auto object_origin(std::size_t object) const noexcept -> ProgramOriginID;

    OwnershipBatchAnalyzer& analysis;
    const OwnershipCallInput& input;
    const SemIRBody& body;
    const SemIRProgram& program;
    const OwnershipBodyFacts& facts;
    bool diagnosing = true;
    std::unique_ptr<OwnershipDiagnosisRecord> diagnosis;
    std::flat_map<LocalBindingID, OwnershipPlace> aliases;
    std::flat_map<LocalBindingID, OwnershipPlace> capture_holders;
    // Bindings can select several possible objects after a join.
    std::flat_map<LocalBindingID, std::vector<OwnershipPlace>> selected_storage;
    std::vector<OwnershipAccess> accesses;
    std::optional<OwnershipFailure> caught;
    std::vector<OwnershipStorageLoan> storage_readers;
    std::optional<LifetimeRegionID> full_expression;
};

struct OwnershipAnalysisSummary final {
    std::size_t query_count;
    // Worklist evaluations only; excludes contract checks.
    std::size_t evaluation_count;
};

class OwnershipBatchAnalyzer final {
public:
    OwnershipBatchAnalyzer(const SemIRProgram& program, AnalysisDiagnostics diagnostics) noexcept;
    auto run() noexcept -> AnalysisResult<OwnershipAnalysisSummary>;
    auto body(BodyID id) const noexcept -> const SemIRBody&;
    auto facts_for_body(BodyID id) const noexcept -> const OwnershipBodyFacts&;
    auto contents(TypeID type) const noexcept -> TypeContents;
    // Answers are borrowed during body evaluation, before the solver updates them.
    auto query(OwnershipCallInput input) noexcept -> std::span<const OwnershipCallCompletion>;

    const SemIRProgram& program;

private:
    auto root_input(const SemIRBody& body) const noexcept -> OwnershipCallInput;
    auto enqueue(std::size_t query) noexcept -> void;
    auto commit_diagnosis(
        std::unique_ptr<OwnershipDiagnosisRecord> diagnosis,
        const OwnershipEscape* escape = nullptr
    ) noexcept -> void;
    // An empty help adds no advice.
    auto diagnose(
        DiagnosticCode code,
        std::string message,
        ProgramOriginID origin,
        std::optional<ProgramOriginID> related,
        std::string related_label,
        std::string help
    ) noexcept -> void;

    AnalysisDiagnostics diagnostics;
    const BodyStore& bodies;
    std::flat_map<BodyID, OwnershipBodyFacts> body_facts;
    std::flat_map<BodyID, std::uint32_t> recursion_components;
    std::vector<std::unique_ptr<OwnershipCallQuery>> queries;
    std::flat_map<BodyID, std::vector<std::size_t>> body_queries;
    std::deque<std::size_t> pending_queries;
    std::optional<std::size_t> active_query;
    std::optional<AnalysisFailure> failure;
    std::map<ProgramOriginID, bool> returned_copies;
};
