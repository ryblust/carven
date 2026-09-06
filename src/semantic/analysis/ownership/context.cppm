module carven:semantic.analysis.ownership.context;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace ownership {

// A missing component denotes an unknown array element. Paths describe storage,
// not the computations that selected it.
using ProjectionPath = std::vector<std::optional<std::uint64_t>>;
struct Place final {
    std::size_t object;
    ProjectionPath path;
    auto operator<=>(const Place&) const noexcept = default;
};
struct Loan final {
    ProjectionPath holder;
    std::optional<Place> backing;
    std::optional<CallableID> callable;
    ProgramOriginID origin;
    bool direct_only;
    auto operator<=>(const Loan& other) const noexcept {
        return std::tie(holder, backing, callable, direct_only)
            <=> std::tie(other.holder, other.backing, other.callable, other.direct_only);
    }
    auto operator==(const Loan& other) const noexcept -> bool { return (*this <=> other) == 0; }
};
struct Capture final {
    ProjectionPath holder;
    Place target;
    ProgramOriginID origin;
    auto operator<=>(const Capture& other) const noexcept {
        return std::tie(holder, target) <=> std::tie(other.holder, other.target);
    }
    auto operator==(const Capture& other) const noexcept -> bool { return (*this <=> other) == 0; }
};
struct Relationships final {
    std::vector<Loan> loans;
    std::vector<Capture> captures;
    auto operator==(const Relationships&) const noexcept -> bool = default;
};
struct ObjectState final {
    bool available = false;
    std::optional<ProgramOriginID> taken;
    Relationships relationships;
    auto operator==(const ObjectState& other) const noexcept -> bool {
        return available == other.available && relationships == other.relationships;
    }
};
struct State final {
    std::vector<ObjectState> objects;
    auto operator==(const State&) const noexcept -> bool = default;
};
enum class ExitKind { Return, Break, Continue, Failure };
struct Exit final {
    ExitKind kind;
    std::optional<TypeID> failure;
    State state;
    Relationships value;
};
struct Flow final {
    std::optional<State> normal;
    Relationships value;
    std::vector<Exit> exits;
};
struct Access final {
    Place place;
    bool stable;
    auto operator<=>(const Access&) const noexcept = default;
};
struct CallArgument final {
    std::optional<Place> alias;
    Relationships value;
    auto operator==(const CallArgument&) const noexcept -> bool = default;
};
struct ExternalObject final {
    TypeID type;
    ProgramOriginID origin;
    ObjectState state;
    auto operator==(const ExternalObject& other) const noexcept -> bool {
        return type == other.type && state == other.state;
    }
};
// Call queries retain only objects reachable from their inputs, with object
// numbers normalized at the boundary. Locals and execution history never enter
// a query key or an answer.
struct CallInput final {
    BodyID body_id;
    std::vector<CallArgument> parameters;
    std::vector<CallArgument> captures;
    std::vector<ExternalObject> objects;
    std::vector<std::vector<bool>> outlives;
    std::vector<Access> accesses;
    auto operator==(const CallInput&) const noexcept -> bool = default;
};
struct CallCompletion final {
    std::optional<TypeID> failure;
    State state;
    Relationships value;
    auto operator==(const CallCompletion&) const noexcept -> bool = default;
};
struct CallQuery final {
    CallInput input;
    std::vector<CallCompletion> answer;
};
struct LocalObject final {
    TypeID type;
    ProgramOriginID origin;
    LifetimeRegionID lifetime;
};

struct CatchAcceptance final {
    std::vector<std::optional<PatternID>> alternatives;
    bool exhaustive;
};
struct BodyFacts final {
    std::vector<LocalObject> locals;
    std::flat_map<const SemanticExpression*, std::size_t> temporaries;
    std::flat_map<LifetimeRegionID, std::vector<std::size_t>> lifetime_objects;
    std::flat_set<PatternID> irrefutable_patterns;
    std::flat_map<const SemCatchArm*, std::flat_map<TypeID, CatchAcceptance>> catches;
};

auto prepare_body_facts(
    const SemIRBody& body,
    const ProgramDraft& draft,
    std::span<const TypeContents> types
) noexcept -> BodyFacts;

auto overlaps(
    std::span<const std::optional<std::uint64_t>> left,
    std::span<const std::optional<std::uint64_t>> right
) noexcept -> bool;
auto overlaps(const Place& left, const Place& right) noexcept -> bool;
auto merge_relationships(Relationships& destination, const Relationships& source) noexcept -> void;
auto project(const Relationships& source, const ProjectionPath& path) noexcept -> Relationships;
auto nested(Relationships source, const ProjectionPath& path) noexcept -> Relationships;
auto join(State& destination, const State& source) noexcept -> void;
auto join_normal(std::optional<State>& destination, const std::optional<State>& source) noexcept
    -> void;
auto append_exits(Flow& destination, Flow& source) noexcept -> void;

class BatchAnalyzer;
class BodyAnalyzer final {
public:
    BodyAnalyzer(BatchAnalyzer& analysis, const CallInput& input, bool diagnosing) noexcept;
    auto run() noexcept -> std::vector<CallCompletion>;
    auto check_contracts() noexcept -> void;

private:
    auto diagnose(
        DiagnosticCode code,
        std::string message,
        ProgramOriginID origin,
        std::optional<ProgramOriginID> related = std::nullopt
    ) noexcept -> void;
    auto outlives(std::size_t source, std::size_t destination) const noexcept -> bool;
    auto leave(Flow& flow, LifetimeRegionID lifetime) const noexcept -> void;
    auto retain(
        State& state,
        const Relationships& relationships,
        const SemanticExpression& source
    ) const noexcept -> void;
    auto use(
        const Relationships& relationships,
        const State& state,
        ProgramOriginID origin,
        bool direct = false
    ) noexcept -> void;
    auto store(
        State& state,
        const Place& target,
        const Relationships& relationships,
        ProgramOriginID origin
    ) noexcept -> void;
    auto references(const Relationships& relationships, const State& state) const noexcept
        -> std::vector<Capture>;
    auto location(const SemanticExpression& source) const noexcept -> std::optional<Place>;
    auto binding_place(LocalBindingID binding) const noexcept -> Place;
    auto is_writable(LocalBindingID binding) const noexcept -> bool;
    auto write_access(const Place& target, ProgramOriginID origin) noexcept -> void;
    auto require_available(const State& state, const Place& place, ProgramOriginID origin) noexcept
        -> void;
    auto constant_truth(const SemanticExpression& source) const noexcept -> std::optional<bool>;
    auto constant_index(const SemanticExpression& source) const noexcept
        -> std::optional<std::uint64_t>;
    auto place(const SemanticExpression& source, State state, bool read = true) noexcept -> Flow;
    auto expression(const SemanticExpression& source, State state, bool direct = false) noexcept
        -> Flow;
    auto complete_expression(const SemanticExpression& source, State state) noexcept -> Flow;
    auto region(const SemanticRegion& source, State state, bool release = true) noexcept -> Flow;
    auto statement(const SemanticStatement& source, State state) noexcept -> Flow;
    auto conditional(const SemIf& value, State state) noexcept -> Flow;
    auto match(const SemMatch& value, State state) noexcept -> Flow;
    auto attempt(const SemTry& value, State state) noexcept -> Flow;
    auto loop(const SemLoop& value, State state) noexcept -> Flow;
    auto range(const SemRangeLoop& value, State state) noexcept -> Flow;
    auto call(
        CallableID callable,
        const Relationships& captures,
        std::span<const CallArgument> parameters,
        State state,
        ProgramOriginID origin
    ) noexcept -> Flow;
    auto bind_pattern(State& state, PatternID pattern, const Relationships& relationships) noexcept
        -> void;
    auto irrefutable(PatternID pattern) const noexcept -> bool;
    auto object_type(std::size_t object) const noexcept -> TypeID;
    auto object_origin(std::size_t object) const noexcept -> ProgramOriginID;
    auto temporary(const SemanticExpression& expression) const noexcept -> std::size_t;

    BatchAnalyzer& analysis;
    const CallInput& input;
    const SemIRBody& body;
    ProgramDraft& draft;
    const BodyFacts& facts;
    bool diagnosing;
    std::flat_map<LocalBindingID, Place> aliases;
    std::vector<Access> accesses;
    std::vector<TypeID> caught;
    std::optional<LifetimeRegionID> full_expression;
};

class BatchAnalyzer final {
public:
    BatchAnalyzer(
        const BodyStore& bodies,
        ProgramDraft& draft,
        std::span<const TypeContents> types
    ) noexcept;
    auto run() noexcept -> AnalysisResult<void>;
    auto body(BodyID id) const noexcept -> const SemIRBody&;
    auto facts_for_body(BodyID id) const noexcept -> const BodyFacts&;
    auto contents(TypeID type) const noexcept -> TypeContents;
    auto query(CallInput input) noexcept -> std::vector<CallCompletion>;
    auto diagnose(
        DiagnosticCode code,
        std::string message,
        ProgramOriginID origin,
        std::optional<ProgramOriginID> related
    ) noexcept -> void;

    ProgramDraft& draft;

private:
    auto root_input(const SemIRBody& body) const noexcept -> CallInput;
    const BodyStore& bodies;
    std::span<const TypeContents> type_contents;
    std::flat_map<BodyID, BodyFacts> body_facts;
    std::vector<std::unique_ptr<CallQuery>> queries;
    std::optional<AnalysisFailure> failure;
};

} // namespace ownership
