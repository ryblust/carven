module carven:semantic.analysis.ownership.context;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.ownership;
import :semantic.analysis.types.contents;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

// A missing component denotes an unknown array element. Paths describe storage,
// not the computations that selected it.
using OwnershipProjectionPath = std::vector<std::optional<std::uint64_t>>;

struct OwnershipPlace final {
    std::size_t object;
    OwnershipProjectionPath path;
    auto operator<=>(const OwnershipPlace&) const noexcept = default;
};

struct OwnershipLoan final {
    OwnershipProjectionPath holder;
    std::optional<OwnershipPlace> backing;
    std::optional<CallableID> callable;
    ProgramOriginID origin;
    bool direct_only;

    auto operator<=>(const OwnershipLoan& other) const noexcept {
        return std::tie(holder, backing, callable, direct_only)
            <=> std::tie(other.holder, other.backing, other.callable, other.direct_only);
    }

    auto operator==(const OwnershipLoan& other) const noexcept -> bool {
        return (*this <=> other) == 0;
    }
};

struct OwnershipCapture final {
    OwnershipProjectionPath holder;
    OwnershipPlace target;
    ProgramOriginID origin;

    auto operator<=>(const OwnershipCapture& other) const noexcept {
        return std::tie(holder, target) <=> std::tie(other.holder, other.target);
    }

    auto operator==(const OwnershipCapture& other) const noexcept -> bool {
        return (*this <=> other) == 0;
    }
};

struct OwnershipRelationships final {
    std::vector<OwnershipLoan> loans;
    std::vector<OwnershipCapture> captures;
    auto operator==(const OwnershipRelationships&) const noexcept -> bool = default;
};

struct OwnershipObjectState final {
    bool available = false;
    std::optional<ProgramOriginID> taken;
    OwnershipRelationships relationships;

    auto operator==(const OwnershipObjectState& other) const noexcept -> bool {
        return available == other.available && relationships == other.relationships;
    }
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
};

struct OwnershipBreak final {};

struct OwnershipContinue final {};

using OwnershipExitPayload =
    std::variant<OwnershipReturn, OwnershipFailure, OwnershipBreak, OwnershipContinue>;

struct OwnershipExit final {
    OwnershipExitPayload payload;
    OwnershipState state;
};

struct OwnershipNormal final {
    OwnershipState state;
    OwnershipRelationships value;
};

struct OwnershipFlow final {
    std::optional<OwnershipNormal> normal;
    std::vector<OwnershipExit> exits;
};

struct OwnershipAccess final {
    OwnershipPlace place;
    bool stable;
    auto operator<=>(const OwnershipAccess&) const noexcept = default;
};

struct OwnershipCallArgument final {
    std::optional<OwnershipPlace> alias;
    OwnershipRelationships value;
    auto operator==(const OwnershipCallArgument&) const noexcept -> bool = default;
};

struct OwnershipExternalObject final {
    TypeID type;
    ProgramOriginID origin;
    OwnershipObjectState state;

    auto operator==(const OwnershipExternalObject& other) const noexcept -> bool {
        return type == other.type && state == other.state;
    }
};

// Call queries retain only objects reachable from their inputs, with object
// numbers normalized at the boundary. Locals and execution history never enter
// a query key or an answer.
struct OwnershipCallInput final {
    BodyID body_id;
    std::vector<OwnershipCallArgument> parameters;
    std::vector<OwnershipCallArgument> captures;
    std::vector<OwnershipExternalObject> objects;
    std::vector<std::vector<bool>> outlives;
    std::vector<OwnershipAccess> accesses;
    auto operator==(const OwnershipCallInput&) const noexcept -> bool = default;
};

struct OwnershipCallCompletion final {
    std::optional<TypeID> failure;
    OwnershipState state;
    OwnershipRelationships value;
    auto operator==(const OwnershipCallCompletion&) const noexcept -> bool = default;
};

struct OwnershipCallQuery final {
    OwnershipCallInput input;
    std::vector<OwnershipCallCompletion> answer;
    std::flat_set<std::size_t> consumers;
    bool queued = false;
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
    std::flat_set<PatternID> irrefutable_patterns;
    std::flat_map<const SemCatchArm*, std::flat_map<TypeID, OwnershipCatchAcceptance>> catches;
};

auto prepare_ownership_body_facts(
    const SemIRBody& body,
    const SemIRProgram& program,
    std::span<const TypeContents> types
) noexcept -> OwnershipBodyFacts;

auto overlaps(
    std::span<const std::optional<std::uint64_t>> left,
    std::span<const std::optional<std::uint64_t>> right
) noexcept -> bool;
auto overlaps(const OwnershipPlace& left, const OwnershipPlace& right) noexcept -> bool;
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
auto append_ownership_exits(OwnershipFlow& destination, OwnershipFlow& source) noexcept -> void;

class OwnershipBatchAnalyzer;

class OwnershipBodyAnalyzer final {
public:
    OwnershipBodyAnalyzer(
        OwnershipBatchAnalyzer& analysis,
        const OwnershipCallInput& input,
        bool diagnosing
    ) noexcept;
    auto run() noexcept -> std::vector<OwnershipCallCompletion>;
    auto check_contracts() noexcept -> void;

private:
    auto diagnose(
        DiagnosticCode code,
        std::string message,
        ProgramOriginID origin,
        std::optional<ProgramOriginID> related = std::nullopt
    ) noexcept -> void;
    auto outlives(std::size_t source, std::size_t destination) const noexcept -> bool;
    auto leave(OwnershipFlow& flow, LifetimeRegionID lifetime) const noexcept -> void;
    auto retain(
        OwnershipState& state,
        const OwnershipRelationships& relationships,
        const SemanticExpression& source
    ) const noexcept -> void;
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
        ProgramOriginID origin
    ) noexcept -> void;
    auto references(
        const OwnershipRelationships& relationships,
        const OwnershipState& state
    ) const noexcept -> std::vector<OwnershipCapture>;
    auto location(const SemanticExpression& source) const noexcept -> std::optional<OwnershipPlace>;
    auto binding_place(LocalBindingID binding) const noexcept -> OwnershipPlace;
    auto is_writable(LocalBindingID binding) const noexcept -> bool;
    auto write_access(const OwnershipPlace& target, ProgramOriginID origin) noexcept -> void;
    auto require_available(
        const OwnershipState& state,
        const OwnershipPlace& place,
        ProgramOriginID origin
    ) noexcept -> void;
    auto constant_truth(const SemanticExpression& source) const noexcept -> std::optional<bool>;
    auto constant_index(const SemanticExpression& source) const noexcept
        -> std::optional<std::uint64_t>;
    auto place(const SemanticExpression& source, OwnershipState state, bool read = true) noexcept
        -> OwnershipFlow;
    auto expression(
        const SemanticExpression& source,
        OwnershipState state,
        bool direct = false
    ) noexcept -> OwnershipFlow;
    auto complete_expression(const SemanticExpression& source, OwnershipState state) noexcept
        -> OwnershipFlow;
    auto region(const SemanticRegion& source, OwnershipState state, bool release = true) noexcept
        -> OwnershipFlow;
    auto statement(const SemanticStatement& source, OwnershipState state) noexcept -> OwnershipFlow;
    auto conditional(const SemIf& value, OwnershipState state) noexcept -> OwnershipFlow;
    auto match(const SemMatch& value, OwnershipState state) noexcept -> OwnershipFlow;
    auto attempt(const SemTry& value, OwnershipState state) noexcept -> OwnershipFlow;
    auto loop(const SemLoop& value, OwnershipState state) noexcept -> OwnershipFlow;
    auto range(const SemRangeLoop& value, OwnershipState state) noexcept -> OwnershipFlow;
    auto call(
        CallableID callable,
        const OwnershipRelationships& captures,
        std::span<const OwnershipCallArgument> parameters,
        OwnershipState state,
        ProgramOriginID origin
    ) noexcept -> OwnershipFlow;
    auto bind_pattern(
        OwnershipState& state,
        PatternID pattern,
        const OwnershipRelationships& relationships
    ) noexcept -> void;
    auto irrefutable(PatternID pattern) const noexcept -> bool;
    auto object_type(std::size_t object) const noexcept -> TypeID;
    auto object_origin(std::size_t object) const noexcept -> ProgramOriginID;
    auto temporary(const SemanticExpression& expression) const noexcept -> std::size_t;

    OwnershipBatchAnalyzer& analysis;
    const OwnershipCallInput& input;
    const SemIRBody& body;
    const SemIRProgram& program;
    const OwnershipBodyFacts& facts;
    bool diagnosing;
    std::flat_map<LocalBindingID, OwnershipPlace> aliases;
    std::vector<OwnershipAccess> accesses;
    std::vector<TypeID> caught;
    std::optional<LifetimeRegionID> full_expression;
};

class OwnershipBatchAnalyzer final {
public:
    OwnershipBatchAnalyzer(
        const SemIRProgram& program,
        AnalysisDiagnostics diagnostics,
        std::span<const TypeContents> types
    ) noexcept;
    auto run() noexcept -> AnalysisResult<void>;
    auto body(BodyID id) const noexcept -> const SemIRBody&;
    auto facts_for_body(BodyID id) const noexcept -> const OwnershipBodyFacts&;
    auto contents(TypeID type) const noexcept -> TypeContents;
    // Answers are borrowed during body evaluation, before the solver updates them.
    auto query(OwnershipCallInput input) noexcept -> std::span<const OwnershipCallCompletion>;
    auto diagnose(
        DiagnosticCode code,
        std::string message,
        ProgramOriginID origin,
        std::optional<ProgramOriginID> related
    ) noexcept -> void;

    const SemIRProgram& program;

private:
    auto root_input(const SemIRBody& body) const noexcept -> OwnershipCallInput;
    auto enqueue(std::size_t query) noexcept -> void;
    AnalysisDiagnostics diagnostics;
    const BodyStore& bodies;
    std::span<const TypeContents> type_contents;
    std::flat_map<BodyID, OwnershipBodyFacts> body_facts;
    std::vector<std::unique_ptr<OwnershipCallQuery>> queries;
    std::flat_map<BodyID, std::vector<std::size_t>> body_queries;
    std::deque<std::size_t> pending_queries;
    std::optional<std::size_t> active_query;
    bool queries_sealed = false;
    std::optional<AnalysisFailure> failure;
};
