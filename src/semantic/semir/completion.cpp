module carven:semantic.semir.completion.impl;

import :semantic.semir.children;
import :semantic.semir.completion;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.invariant;
import std;

// The opaque query state owns this implementation by value, requiring module linkage.
class Completion final {
public:
    struct Flow final {
        Flow() noexcept;
        explicit Flow(ExitSet exits) noexcept;
        explicit Flow(Exit exit) noexcept;
        static auto failure(ConstructionTypeRef type) noexcept -> Flow;
        auto contains(Exit exit) const noexcept -> bool;
        auto without(Exit exit) const noexcept -> Flow;
        auto operator|(const Flow& other) const noexcept -> Flow;
        auto operator|(Exit other) const noexcept -> Flow;
        auto operator&(ExitSet mask) const noexcept -> Flow;
        auto then(const Flow& next) const noexcept -> Flow;
        auto then(ExitSet next) const noexcept -> Flow;
        auto then(Exit next) const noexcept -> Flow;

        ExitSet exits;
        std::vector<ConstructionTypeRef> failure_types;
        bool unknown_failure;
    };

    struct PatternFlow final {
        bool accepted;
        bool rejected;
        Flow outward;
    };

    class CatchEntries final {
    public:
        explicit CatchEntries(const Flow& body) noexcept;
        auto entry(std::optional<ConstructionTypeRef> type) noexcept -> bool;
        auto consume(std::optional<ConstructionTypeRef> type, const PatternFlow& pattern) noexcept
            -> void;
        auto accepted() const noexcept -> bool;
        auto finish(bool guard_may_reject) noexcept -> void;
        auto residual() const noexcept -> Flow;

    private:
        struct Pending final {
            ConstructionTypeRef type;
            bool pending;
            bool accepted;
        };

        std::vector<Pending> types;
        bool unknown_pending;
        bool unknown_accepted;
    };

    explicit Completion(const CompletionPatterns& patterns, bool retain_patterns = false) noexcept;

    template<typename Node>
    auto evaluate(const Node& source) noexcept -> ExitSet {
        SemanticTraversal<true, Completion> {*this}(source);
        return get(source).exits;
    }

    template<typename Node>
    auto get(const Node& source) const noexcept -> const Flow& {
        return facts.at(std::addressof(source));
    }

    template<typename Node>
    auto node_flow(const Node& source) const noexcept -> Flow {
        // Expanded copies can share source PatternIDs while owning different
        // rewritten bounds. Only the incremental pattern query retains IDs.
        auto query = Completion(patterns);
        SemanticTraversal<true, Completion> {query}(source);
        return query.get(source);
    }

    auto leave(const SemanticExpression& source) noexcept -> void;
    auto leave(const SemanticStatement& source) noexcept -> void;
    auto leave(const SemanticRegion& source) noexcept -> void;
    auto pattern(PatternID id, std::span<const SemPatternBounds> bounds) noexcept -> PatternFlow;
    auto evaluate_pattern(PatternID id, std::span<const SemPatternBounds> bounds) noexcept
        -> PatternCompletion;
    auto add_bounds(std::span<const SemPatternBounds> bounds) noexcept -> void;
    auto region_flow(const SemanticRegion& body) noexcept -> Flow;

private:
    const CompletionPatterns& patterns;
    bool retain_patterns;
    std::unordered_map<const void*, Flow> facts;
    std::map<PatternID, PatternFlow> pattern_facts;
};

Completion::Flow::Flow() noexcept
    : Flow(ExitSet()) {}

Completion::Flow::Flow(ExitSet exits) noexcept
    : exits(exits),
      failure_types(),
      unknown_failure(exits.contains(Exit::Failure)) {}

Completion::Flow::Flow(Exit exit) noexcept
    : Flow(ExitSet(exit)) {}

auto Completion::Flow::failure(ConstructionTypeRef type) noexcept -> Flow {
    auto result = Flow(Exit::Failure);
    result.unknown_failure = false;
    result.failure_types.push_back(type);
    return result;
}

auto Completion::Flow::contains(Exit exit) const noexcept -> bool {
    return exits.contains(exit);
}

auto Completion::Flow::without(Exit exit) const noexcept -> Flow {
    auto result = *this;
    result.exits = exits.without(exit);
    if (exit == Exit::Failure) {
        result.failure_types.clear();
        result.unknown_failure = false;
    }
    return result;
}

auto Completion::Flow::operator|(const Flow& other) const noexcept -> Flow {
    auto result = *this;
    result.exits = exits | other.exits;
    result.unknown_failure |= other.unknown_failure;
    for (const auto type : other.failure_types) {
        if (!std::ranges::contains(result.failure_types, type)) {
            result.failure_types.push_back(type);
        }
    }
    return result;
}

auto Completion::Flow::operator|(Exit other) const noexcept -> Flow {
    return *this | Flow(other);
}

auto Completion::Flow::operator&(ExitSet mask) const noexcept -> Flow {
    auto result = *this;
    result.exits = exits & mask;
    if (!mask.contains(Exit::Failure)) {
        result.failure_types.clear();
        result.unknown_failure = false;
    }
    return result;
}

auto Completion::Flow::then(const Flow& next) const noexcept -> Flow {
    return contains(Exit::Normal) ? without(Exit::Normal) | next : *this;
}

auto Completion::Flow::then(ExitSet next) const noexcept -> Flow {
    return then(Flow(next));
}

auto Completion::Flow::then(Exit next) const noexcept -> Flow {
    return then(Flow(next));
}

Completion::CatchEntries::CatchEntries(const Flow& body) noexcept
    : types(),
      unknown_pending(body.unknown_failure),
      unknown_accepted(false) {
    for (const auto type : body.failure_types) {
        types.push_back({.type = type, .pending = true, .accepted = false});
    }
}

auto Completion::CatchEntries::entry(std::optional<ConstructionTypeRef> type) noexcept -> bool {
    if (!type) {
        return unknown_pending || std::ranges::any_of(types, &Pending::pending);
    }
    const auto found = std::ranges::find(types, *type, &Pending::type);
    if (found != types.end()) {
        return found->pending;
    }
    // An unknown call failure is split only when a handler names its type.
    types.push_back({.type = *type, .pending = unknown_pending, .accepted = false});
    return unknown_pending;
}

auto Completion::CatchEntries::consume(
    std::optional<ConstructionTypeRef> type,
    const PatternFlow& pattern
) noexcept -> void {
    if (type) {
        const auto found = std::ranges::find(types, *type, &Pending::type);
        if (found == types.end()) {
            invariant_violation("catch consumption has no attempted failure type");
        }
        if (found->pending) {
            found->accepted |= pattern.accepted;
            found->pending = pattern.rejected;
        }
    } else {
        unknown_accepted |= unknown_pending;
        unknown_pending = false;
        for (auto& item : types) {
            item.accepted |= item.pending;
            item.pending = false;
        }
    }
}

auto Completion::CatchEntries::accepted() const noexcept -> bool {
    return unknown_accepted || std::ranges::any_of(types, &Pending::accepted);
}

auto Completion::CatchEntries::finish(bool guard_may_reject) noexcept -> void {
    unknown_pending |= unknown_accepted && guard_may_reject;
    unknown_accepted = false;
    for (auto& item : types) {
        item.pending |= item.accepted && guard_may_reject;
        item.accepted = false;
    }
}

auto Completion::CatchEntries::residual() const noexcept -> Flow {
    auto result = unknown_pending ? Flow(Exit::Failure) : Flow();
    for (const auto& item : types) {
        if (item.pending) {
            result = result | Flow::failure(item.type);
        }
    }
    return result;
}

Completion::Completion(const CompletionPatterns& patterns, bool retain_patterns) noexcept
    : patterns(patterns),
      retain_patterns(retain_patterns) {}

namespace {

constexpr auto outward = ExitSet(Exit::Return) | Exit::Failure | Exit::Stop;

} // namespace

auto Completion::pattern(PatternID root, std::span<const SemPatternBounds> bounds) noexcept
    -> PatternFlow {
    if (!patterns.read || !patterns.single_case) {
        invariant_violation("pattern completion requires its body owner");
    }
    if (!retain_patterns) {
        pattern_facts.clear();
    }
    using Value = std::variant<PatternValue, ElaboratedPatternValue>;

    struct Pending final {
        PatternID id;
        Value value;
        bool children_complete;
    };

    if (const auto found = pattern_facts.find(root); found != pattern_facts.end()) {
        return found->second;
    }
    auto pending = std::vector<Pending> {
        {.id = root, .value = patterns.read(root), .children_complete = false}
    };
    auto& completed = pattern_facts;
    while (!pending.empty()) {
        auto& item = pending.back();
        if (!item.children_complete) {
            item.children_complete = true;
            auto children = std::vector<PatternID>();
            item.value.visit([&](const auto& value) noexcept {
                value.visit([&](const auto& operation) noexcept {
                    using Operation = std::remove_cvref_t<decltype(operation)>;
                    if constexpr (std::same_as<Operation, OrPattern>) {
                        children = operation.alternatives;
                    } else if constexpr (std::same_as<Operation, EnumCasePattern>) {
                        children = operation.payload;
                    }
                });
            });
            for (const auto child : children) {
                if (!completed.contains(child)) {
                    pending.push_back(
                        {.id = child, .value = patterns.read(child), .children_complete = false}
                    );
                }
            }
            continue;
        }
        const auto flow = item.value.visit([&](const auto& value) noexcept {
            return value.visit([&](const auto& operation) noexcept -> PatternFlow {
                using Operation = std::remove_cvref_t<decltype(operation)>;
                auto result = PatternFlow {.accepted = true, .rejected = false, .outward = {}};
                if constexpr (std::same_as<Operation, OrPattern>) {
                    result.accepted = false;
                    result.rejected = true;
                    for (const auto child : operation.alternatives) {
                        if (!result.rejected) {
                            break;
                        }
                        const auto next = completed.at(child);
                        result.accepted |= next.accepted;
                        result.rejected = next.rejected;
                        result.outward = result.outward | next.outward;
                    }
                } else if constexpr (std::same_as<Operation, EnumCasePattern>) {
                    result.rejected = !patterns.single_case(operation.enum_case);
                    for (const auto child : operation.payload) {
                        if (!result.accepted) {
                            break;
                        }
                        const auto next = completed.at(child);
                        result.accepted = next.accepted;
                        result.rejected |= next.rejected;
                        result.outward = result.outward | next.outward;
                    }
                } else if constexpr (std::same_as<Operation, RangePattern>) {
                    auto evaluation = Flow(Exit::Normal);
                    const auto found =
                        std::ranges::find(bounds, item.id, &SemPatternBounds::pattern);
                    if (found == bounds.end()
                        && ((operation.begin && !operation.begin->constant)
                            || (operation.end && !operation.end->constant))) {
                        invariant_violation("dynamic pattern completion requires its bounds");
                    }
                    if (found != bounds.end()) {
                        for (const auto* bound : {&found->begin, &found->end}) {
                            if (*bound) {
                                evaluation = evaluation.then(get(**bound));
                            }
                        }
                    }
                    result.accepted = evaluation.contains(Exit::Normal);
                    result.rejected = result.accepted && (operation.begin || operation.end);
                    result.outward = evaluation.without(Exit::Normal);
                } else if constexpr (std::same_as<Operation, LiteralPattern>) {
                    result.rejected = true;
                }
                return result;
            });
        });
        completed.insert_or_assign(item.id, flow);
        pending.pop_back();
    }
    return completed.at(root);
}

auto Completion::add_bounds(std::span<const SemPatternBounds> bounds) noexcept -> void {
    for (const auto& bound : bounds) {
        if (pattern_facts.contains(bound.pattern)) {
            continue;
        }
        if (bound.begin) {
            facts.emplace(&*bound.begin, node_flow(*bound.begin));
        }
        if (bound.end) {
            facts.emplace(&*bound.end, node_flow(*bound.end));
        }
        static_cast<void>(pattern(bound.pattern, std::span(&bound, 1)));
    }
    facts.clear();
}

auto Completion::evaluate_pattern(PatternID id, std::span<const SemPatternBounds> bounds) noexcept
    -> PatternCompletion {
    if (const auto found = pattern_facts.find(id); found != pattern_facts.end()) {
        return {.accepted = found->second.accepted, .rejected = found->second.rejected};
    }
    add_bounds(bounds);
    const auto result = pattern(id, bounds);
    // Bounds may move when construction adds another pattern. Only completed
    // pattern values remain useful across queries.
    facts.clear();
    return {.accepted = result.accepted, .rejected = result.rejected};
}

auto Completion::region_flow(const SemanticRegion& body) noexcept -> Flow {
    return node_flow(body);
}

namespace {

using Flow = Completion::Flow;
using PatternFlow = Completion::PatternFlow;
using CatchEntries = Completion::CatchEntries;

auto conditional_exits(const SemIf& conditional, const Completion& facts) noexcept -> Flow {
    auto result = Flow();
    // Whether control can still reach the next condition.
    auto pending = true;
    for (const auto& branch : conditional.branches) {
        if (!pending) {
            break;
        }
        const auto& condition = facts.get(branch.condition);
        result = result | condition.then(facts.get(branch.body));
        pending = condition.contains(Exit::Normal);
    }
    if (!pending) {
        return result;
    }
    return result
        | (conditional.otherwise ? facts.get(**conditional.otherwise) : Flow(Exit::Normal));
}

auto call_exits(const SemCall& call, const Completion& facts) noexcept -> Flow {
    auto result = facts.get(*call.callee);
    for (const auto& argument : call.arguments) {
        result = result.then(facts.get(argument.expression));
    }
    // Inferred callable failure terms can still grow during construction.
    // Completion therefore keeps the call's failure type unknown here.
    return result.then(ExitSet(Exit::Normal) | Exit::Failure);
}

auto match_exits(const SemMatch& match, Completion& facts) noexcept -> Flow {
    const auto subject = facts.get(*match.subject);
    auto result = subject.without(Exit::Normal);
    if (!subject.contains(Exit::Normal)) {
        return result;
    }
    auto pending = true;
    for (const auto& arm : match.arms) {
        if (!pending || !arm.reachable) {
            continue;
        }
        auto selected = facts.pattern(arm.pattern, arm.pattern_bounds);
        if (!arm.pattern_may_reject) {
            selected.rejected = false;
        }
        result = result | selected.outward;
        pending = selected.rejected;
        if (selected.accepted) {
            const auto guard = arm.guard ? facts.get(*arm.guard) : Flow(Exit::Normal);
            result = result | guard.then(facts.get(arm.body));
            pending |= arm.guard && guard.contains(Exit::Normal);
        }
    }
    return result;
}

auto try_exits(const SemTry& attempt, Completion& facts) noexcept -> Flow {
    const auto& body = facts.get(*attempt.body);
    if (!body.contains(Exit::Failure)) {
        return body;
    }
    auto result = body.without(Exit::Failure);
    auto entries = CatchEntries(body);
    for (const auto& arm : attempt.arms) {
        for (const auto& alternative : arm.alternatives) {
            if (!alternative.reachable) {
                continue;
            }
            const auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern);
            const auto type = typed ? std::optional(typed->type.construction()) : std::nullopt;
            if (!entries.entry(type)) {
                continue;
            }
            const auto selected = typed
                ? facts.pattern(typed->inner, arm.pattern_bounds)
                : PatternFlow {.accepted = true, .rejected = false, .outward = {}};
            result = result | selected.outward;
            entries.consume(type, selected);
        }
        const auto guard = arm.guard ? facts.get(*arm.guard) : Flow(Exit::Normal);
        if (entries.accepted()) {
            // Predicate, guard, and body failures leave this try. Only rejection
            // of the protected failure can enter another handler.
            result = result | guard.then(facts.get(arm.body));
        }
        entries.finish(arm.guard && guard.contains(Exit::Normal));
    }
    return result | entries.residual();
}

auto loop_exits(const SemLoop& loop, const Completion& facts) noexcept -> Flow {
    const auto condition = loop.condition ? facts.get(*loop.condition) : Flow(Exit::Normal);
    const auto& body = facts.get(*loop.body);
    auto result = condition & outward;
    if (loop.condition && condition.contains(Exit::Normal)) {
        result = result | Exit::Normal;
    }
    if (condition.contains(Exit::Normal)) {
        result = result | (body & outward);
        if (body.contains(Exit::Normal) || body.contains(Exit::Continue)) {
            result = result | (facts.get(*loop.steps) & outward);
        }
        if (body.contains(Exit::Break)) {
            result = result | Exit::Normal;
        }
    }
    return facts.get(*loop.initializer).then(result);
}

auto expanded_exits(const SemExpandedLoop& loop, const Completion& facts) noexcept -> Flow {
    auto result = Flow();
    for (const auto& iteration : loop.iterations) {
        const auto& body = facts.get(iteration);
        result = result | (body & outward);
        if (body.contains(Exit::Break)) {
            result = result | Exit::Normal;
        }
        // Continue enters the next copy. A copy that neither completes nor
        // continues ends the expansion.
        if (!body.contains(Exit::Normal) && !body.contains(Exit::Continue)) {
            return result;
        }
    }
    return result | Exit::Normal;
}

} // namespace

auto Completion::leave(const SemanticExpression& expression) noexcept -> void {
    auto& facts = *this;
    auto result = expression.value.visit([&](const auto& operation) noexcept -> Flow {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemIf>) {
            return conditional_exits(operation, facts);
        } else if constexpr (std::same_as<Operation, SemCall>) {
            return call_exits(operation, facts);
        } else if constexpr (std::same_as<Operation, SemTry>) {
            return try_exits(operation, facts);
        } else if constexpr (std::same_as<Operation, SemMatch>) {
            return match_exits(operation, facts);
        } else if constexpr (std::same_as<Operation, SemShortCircuit>) {
            return facts.get(*operation.left)
                .then(Flow(Exit::Normal) | facts.get(*operation.right));
        } else if constexpr (std::same_as<Operation, SemReport>) {
            const auto message =
                operation.message ? facts.get(**operation.message) : Flow(Exit::Normal);
            if (operation.kind == ReportKind::Fail) {
                return message.then(Exit::Stop);
            }
            const auto condition =
                operation.condition ? facts.get(**operation.condition) : Flow(Exit::Normal);
            // A conditional report can return without evaluating its message.
            return condition.then(
                Flow(Exit::Normal)
                | message.then(
                    operation.kind == ReportKind::Check ? ExitSet(Exit::Normal)
                                                        : ExitSet(Exit::Stop)
                )
            );
        } else if constexpr (std::same_as<Operation, SemUnreachable>) {
            return Flow();
        } else {
            auto result = Flow(Exit::Normal);
            visit_semantic_children(operation, [&](const auto& child) noexcept {
                result = result.then(facts.get(child));
            });
            return result;
        }
    });
    this->facts.emplace(&expression, result);
}

auto Completion::leave(const SemanticStatement& statement) noexcept -> void {
    const auto& facts = *this;
    auto result = statement.value.visit([&](const auto& operation) noexcept -> Flow {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemReturn>) {
            return operation.value ? facts.get(*operation.value).then(Exit::Return)
                                   : Flow(Exit::Return);
        } else if constexpr (std::same_as<Operation, SemBreak>) {
            return Flow(Exit::Break);
        } else if constexpr (std::same_as<Operation, SemContinue>) {
            return Flow(Exit::Continue);
        } else if constexpr (std::same_as<Operation, SemRethrow>) {
            // Rethrow inherits a handler's failure type; the tree does not own
            // that enclosing context, so retain an unknown outward failure.
            return Flow(Exit::Failure);
        } else if constexpr (std::same_as<Operation, SemThrow>) {
            return facts.get(operation.value).then(Flow::failure(operation.failure_type));
        } else if constexpr (std::same_as<Operation, SemExpressionStatement>) {
            return facts.get(operation.expression);
        } else if constexpr (std::same_as<Operation, SemInitialize>) {
            return facts.get(operation.initializer);
        } else if constexpr (std::same_as<Operation, SemStaticBinding>
                             || std::same_as<Operation, SemConstBlock>) {
            // The static stage leaves nothing behind in executable code.
            return Flow(Exit::Normal);
        } else if constexpr (std::same_as<Operation, SemAssign>) {
            return facts.get(operation.target).then(facts.get(operation.value));
        } else if constexpr (std::same_as<Operation, SemLoop>) {
            return loop_exits(operation, facts);
        } else if constexpr (std::same_as<Operation, SemRangeLoop>) {
            return facts.get(operation.source)
                .then(Flow(Exit::Normal) | (facts.get(*operation.body) & outward));
        } else if constexpr (std::same_as<Operation, SemExpandedLoop>) {
            return expanded_exits(operation, facts);
        } else {
            static_assert(std::same_as<Operation, OwnedSemanticRegion>);
            return facts.get(*operation);
        }
    });
    this->facts.emplace(&statement, result);
}

auto Completion::leave(const SemanticRegion& region) noexcept -> void {
    const auto& facts = *this;
    auto result = Flow(Exit::Normal);
    for (const auto& statement : region.statements) {
        result = result.then(facts.get(statement));
    }
    this->facts.emplace(&region, region.result ? result.then(facts.get(*region.result)) : result);
}

struct CompletionQuery::State final {
    explicit State(CompletionPatterns patterns) noexcept;

    CompletionPatterns patterns;
    Completion completion;
    std::unique_ptr<Completion::CatchEntries> catches;
};

CompletionQuery::State::State(CompletionPatterns patterns) noexcept
    : patterns(std::move(patterns)),
      completion(this->patterns, true),
      catches() {}

CompletionQuery::CompletionQuery(
    CompletionPatterns patterns,
    std::span<const SemPatternBounds> bounds
) noexcept
    : state(std::make_unique<State>(std::move(patterns))) {
    state->completion.add_bounds(bounds);
}

CompletionQuery::~CompletionQuery() noexcept = default;

auto CompletionQuery::pattern(PatternID pattern, std::span<const SemPatternBounds> bounds) noexcept
    -> PatternCompletion {
    return state->completion.evaluate_pattern(pattern, bounds);
}

auto CompletionQuery::enter_try(const SemanticRegion& body) noexcept -> void {
    state->catches =
        std::make_unique<Completion::CatchEntries>(state->completion.region_flow(body));
}

auto CompletionQuery::catch_entry(std::optional<ConstructionTypeRef> type) noexcept -> bool {
    return state->catches->entry(type);
}

auto CompletionQuery::consume_catch(
    std::optional<ConstructionTypeRef> type,
    std::optional<PatternID> pattern
) noexcept -> void {
    state->catches->consume(
        type,
        pattern ? state->completion.pattern(*pattern, {})
                : Completion::PatternFlow {.accepted = true, .rejected = false, .outward = {}}
    );
}

auto CompletionQuery::catch_accepted() const noexcept -> bool {
    return state->catches->accepted();
}

auto CompletionQuery::finish_catch(bool guard_may_reject) noexcept -> void {
    state->catches->finish(guard_may_reject);
}

auto CompletionQuery::pending_failures() const noexcept
    -> std::optional<std::vector<ConstructionTypeRef>> {
    const auto residual = state->catches->residual();
    return residual.unknown_failure ? std::nullopt : std::optional(residual.failure_types);
}

auto exits(const SemanticExpression& expression, const CompletionPatterns& patterns) noexcept
    -> ExitSet {
    return Completion(patterns).evaluate(expression);
}

auto exits(const SemanticStatement& statement, const CompletionPatterns& patterns) noexcept
    -> ExitSet {
    return Completion(patterns).evaluate(statement);
}

auto exits(const SemanticRegion& region, const CompletionPatterns& patterns) noexcept -> ExitSet {
    return Completion(patterns).evaluate(region);
}
