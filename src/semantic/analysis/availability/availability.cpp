module carven:semantic.analysis.availability.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.analyzer;
import :semantic.analysis.availability;
import :semantic.analysis.call_contract;
import :semantic.analysis.session;
import :semantic.analysis.session.read;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct AvailabilityBlockID final {
    std::uint32_t value;
    constexpr auto operator==(const AvailabilityBlockID&) const noexcept -> bool = default;
};

struct AvailabilityPlaceID final {
    std::uint32_t value;
    constexpr auto operator==(const AvailabilityPlaceID&) const noexcept -> bool = default;
};

struct TakeSiteID final {
    ProgramOriginID origin;
    std::uint32_t source;
    std::uint32_t offset;
    std::uint32_t expression;

    constexpr auto operator==(const TakeSiteID& other) const noexcept -> bool {
        return source == other.source && offset == other.offset && expression == other.expression;
    }

    constexpr auto operator<=>(const TakeSiteID& other) const noexcept {
        return std::tuple(source, offset, expression)
            <=> std::tuple(other.source, other.offset, other.expression);
    }
};

struct AvailabilityPlaceRef final {
    BodyID body;
    AvailabilityPlaceID local;
};

class AvailabilityPlaceCatalog final {
public:
    explicit AvailabilityPlaceCatalog(SemanticDraftView hir) noexcept
        : place_refs(hir.symbol_count()),
          body_places(hir.bodies().size()) {
        auto roots = std::vector<std::optional<BodyID>>(hir.scopes().size());
        for (auto index = 0uz; index < hir.bodies().size(); ++index) {
            const auto body = BodyID::from_index(static_cast<std::uint32_t>(index));
            const auto scope = hir.body(body).scope;
            if (scope.index() >= roots.size() || roots[scope.index()].has_value()) {
                invariant_violation("validated semantic bodies do not have unique root scopes");
            }
            roots[scope.index()] = body;
        }

        auto scope_bodies = std::vector<std::optional<BodyID>>(hir.scopes().size());
        for (auto index = 0uz; index < hir.scopes().size(); ++index) {
            const auto& scope = hir.scopes()[index];
            if (scope.parent.has_value()) {
                if (scope.parent->index() >= index) {
                    invariant_violation("validated semantic scopes are not construction-ordered");
                }
                scope_bodies[index] = scope_bodies[scope.parent->index()];
            }
            if (roots[index].has_value()) {
                scope_bodies[index] = roots[index];
            }
        }

        for (auto index = 0uz; index < hir.symbol_count(); ++index) {
            const auto symbol = SymbolID::from_index(static_cast<std::uint32_t>(index));
            const auto& binding = hir.binding(symbol);
            if (!binding.has_value()) {
                continue;
            }
            const auto scope = binding->scope;
            if (scope.index() >= scope_bodies.size()) {
                invariant_violation("validated semantic binding has an unknown scope");
            }
            const auto body = scope_bodies[scope.index()];
            if (!body.has_value()) {
                continue;
            }
            auto& places = body_places[body->index()];
            const auto local = AvailabilityPlaceID {
                .value = static_cast<std::uint32_t>(places.size()),
            };
            places.push_back(symbol);
            place_refs[index] = AvailabilityPlaceRef {.body = *body, .local = local};
        }
    }

    auto local(BodyID body, SymbolID symbol) const noexcept -> AvailabilityPlaceID {
        if (symbol.index() >= place_refs.size()
            || !place_refs[symbol.index()].has_value()
            || place_refs[symbol.index()]->body != body) {
            invariant_violation("availability references a place owned by another body");
        }
        return place_refs[symbol.index()]->local;
    }

    auto global(BodyID body, AvailabilityPlaceID place) const noexcept -> SymbolID {
        const auto& places = body_places[body.index()];
        if (place.value >= places.size()) {
            invariant_violation("availability references an unknown body-local place");
        }
        return places[place.value];
    }

    auto count(BodyID body) const noexcept -> std::size_t {
        return body_places[body.index()].size();
    }

private:
    std::vector<std::optional<AvailabilityPlaceRef>> place_refs;
    std::vector<std::vector<SymbolID>> body_places;
};

struct AvailabilityUse final {
    HIRExprID expression;
    AvailabilityPlaceID place;
};

struct AvailabilityTake final {
    HIRExprID expression;
    AvailabilityPlaceID place;
    TakeSiteID site;
};

struct AvailabilityRestore final {
    AvailabilityPlaceID place;
    bool requires_write;
};

struct AvailabilityCapture final {
    HIRExprID expression;
    AvailabilityPlaceID source;
    ProgramOriginID origin;
    bool write;
};

struct AvailabilityOperationCheck final {
    HIRExprID primary;
    std::optional<HIRExprID> related;
    std::vector<AvailabilityPlaceID> reads;
    std::vector<AvailabilityPlaceID> writes;
    std::vector<AvailabilityPlaceID> takes;
};

enum class InvalidTakeKind {
    Partial,
    NonOwner,
};

struct AvailabilityInvalidTake final {
    ProgramOriginID origin;
    InvalidTakeKind kind;
};

using AvailabilityOperation = std::variant<
    AvailabilityUse,
    AvailabilityTake,
    AvailabilityRestore,
    AvailabilityCapture,
    AvailabilityOperationCheck,
    AvailabilityInvalidTake>;

struct AvailabilityBlock final {
    std::uint32_t operation_begin;
    std::uint32_t operation_count;
    std::uint32_t successor_begin;
    std::uint32_t successor_count;
};

struct BodyAvailabilityGraph final {
    AvailabilityBlockID entry;
    std::vector<AvailabilityBlock> blocks;
    std::vector<AvailabilityOperation> operations;
    std::vector<AvailabilityBlockID> successors;
};

struct MutableAvailabilityBlock final {
    std::vector<AvailabilityOperation> operations;
    std::vector<AvailabilityBlockID> successors;
};

class MutableAvailabilityGraph final {
public:
    auto append(
        std::vector<AvailabilityOperation> operations = {},
        std::vector<AvailabilityBlockID> successors = {}
    ) noexcept -> AvailabilityBlockID {
        normalize_successors(successors);
        const auto id = AvailabilityBlockID {
            .value = static_cast<std::uint32_t>(blocks.size()),
        };
        blocks.push_back({
            .operations = std::move(operations),
            .successors = std::move(successors),
        });
        return id;
    }

    auto set_successors(
        AvailabilityBlockID id,
        std::vector<AvailabilityBlockID> successors
    ) noexcept -> void {
        normalize_successors(successors);
        blocks[id.value].successors = std::move(successors);
    }

    auto freeze(AvailabilityBlockID entry) && noexcept -> BodyAvailabilityGraph {
        auto reachable = std::vector<std::uint8_t>(blocks.size(), 0);
        auto pending = std::vector<AvailabilityBlockID> {entry};
        while (!pending.empty()) {
            const auto block = pending.back();
            pending.pop_back();
            if (reachable[block.value] != 0) {
                continue;
            }
            reachable[block.value] = 1;
            for (const auto successor : blocks[block.value].successors) {
                pending.push_back(successor);
            }
        }

        auto indegree = std::vector<std::uint32_t>(blocks.size(), 0);
        for (auto index = 0uz; index < blocks.size(); ++index) {
            if (reachable[index] == 0) {
                continue;
            }
            for (const auto successor : blocks[index].successors) {
                if (reachable[successor.value] != 0) {
                    ++indegree[successor.value];
                }
            }
        }

        constexpr auto unassigned = std::numeric_limits<std::uint32_t>::max();
        auto frozen_of = std::vector<std::uint32_t>(blocks.size(), unassigned);
        auto chains = std::vector<std::vector<AvailabilityBlockID>>();
        const auto append_chain = [&](AvailabilityBlockID start) noexcept {
            const auto frozen = static_cast<std::uint32_t>(chains.size());
            auto chain = std::vector<AvailabilityBlockID>();
            auto current = start;
            while (true) {
                frozen_of[current.value] = frozen;
                chain.push_back(current);
                const auto& source = blocks[current.value];
                if (source.successors.size() != 1) {
                    break;
                }
                const auto next = source.successors.front();
                if (reachable[next.value] == 0
                    || indegree[next.value] != 1
                    || frozen_of[next.value] != unassigned) {
                    break;
                }
                current = next;
            }
            chains.push_back(std::move(chain));
        };

        append_chain(entry);
        for (auto index = 0uz; index < blocks.size(); ++index) {
            if (reachable[index] != 0 && frozen_of[index] == unassigned) {
                append_chain(
                    AvailabilityBlockID {
                        .value = static_cast<std::uint32_t>(index),
                    }
                );
            }
        }

        auto result = BodyAvailabilityGraph {
            .entry = AvailabilityBlockID {.value = frozen_of[entry.value]},
            .blocks = {},
            .operations = {},
            .successors = {},
        };
        result.blocks.reserve(chains.size());
        for (const auto& chain : chains) {
            const auto operation_begin = static_cast<std::uint32_t>(result.operations.size());
            for (const auto source : chain) {
                auto& operations = blocks[source.value].operations;
                result.operations.insert(
                    result.operations.end(),
                    std::make_move_iterator(operations.begin()),
                    std::make_move_iterator(operations.end())
                );
            }
            const auto successor_begin = static_cast<std::uint32_t>(result.successors.size());
            auto successors = std::vector<AvailabilityBlockID>();
            for (const auto successor : blocks[chain.back().value].successors) {
                successors.push_back(
                    AvailabilityBlockID {
                        .value = frozen_of[successor.value],
                    }
                );
            }
            normalize_successors(successors);
            result.successors.insert(result.successors.end(), successors.begin(), successors.end());
            result.blocks.push_back({
                .operation_begin = operation_begin,
                .operation_count =
                    static_cast<std::uint32_t>(result.operations.size()) - operation_begin,
                .successor_begin = successor_begin,
                .successor_count =
                    static_cast<std::uint32_t>(result.successors.size()) - successor_begin,
            });
        }
        return result;
    }

private:
    static auto normalize_successors(std::vector<AvailabilityBlockID>& successors) noexcept
        -> void {
        std::ranges::sort(successors, {}, &AvailabilityBlockID::value);
        successors.erase(std::ranges::unique(successors).begin(), successors.end());
    }

    std::vector<MutableAvailabilityBlock> blocks;
};

struct AvailabilityTargets final {
    AvailabilityBlockID returned;
    AvailabilityBlockID broken;
    AvailabilityBlockID continued;
    AvailabilityBlockID test_exit;
    AvailabilityBlockID failure_fallback;
    std::vector<std::pair<HIRTypeID, AvailabilityBlockID>> failures;
    std::vector<HIRTypeID> rethrows;
};

class BodyAvailabilityGraphBuilder final {
public:
    BodyAvailabilityGraphBuilder(
        SemanticDraftView hir,
        const AvailabilityPlaceCatalog& catalog,
        BodyID body
    ) noexcept
        : hir(hir),
          catalog(catalog),
          body(body) {}

    auto build() && noexcept -> BodyAvailabilityGraph {
        const auto terminal = graph.append();
        const auto targets = AvailabilityTargets {
            .returned = terminal,
            .broken = terminal,
            .continued = terminal,
            .test_exit = terminal,
            .failure_fallback = terminal,
            .failures = {},
            .rethrows = {},
        };
        const auto entry = lower_block(hir.body(body).root, terminal, targets);
        return std::move(graph).freeze(entry);
    }

private:
    auto local(SymbolID symbol) const noexcept -> AvailabilityPlaceID {
        return catalog.local(body, symbol);
    }

    auto required_place(SymbolID symbol) const noexcept -> AvailabilityPlaceID {
        if (!hir.binding(symbol).has_value()) {
            invariant_violation("runtime binding has no published semantic facts");
        }
        return local(symbol);
    }

    auto whole_place(HIRExprID expression) const noexcept -> std::optional<SymbolID> {
        const auto& use = hir.place_use(expression);
        return use.has_value() && use->projections.empty() ? std::optional(use->root)
                                                           : std::nullopt;
    }

    auto prepend(AvailabilityOperation operation, AvailabilityBlockID next) noexcept
        -> AvailabilityBlockID {
        return graph.append({std::move(operation)}, {next});
    }

    auto branch(std::vector<AvailabilityBlockID> successors) noexcept -> AvailabilityBlockID {
        std::ranges::sort(successors, {}, &AvailabilityBlockID::value);
        successors.erase(std::ranges::unique(successors).begin(), successors.end());
        if (successors.size() == 1) {
            return successors.front();
        }
        return graph.append({}, std::move(successors));
    }

    auto failure_target(HIRTypeID failure, const AvailabilityTargets& targets) const noexcept
        -> AvailabilityBlockID {
        const auto found = std::ranges::lower_bound(
            targets.failures,
            failure.index(),
            {},
            [](const auto& entry) static noexcept { return entry.first.index(); }
        );
        return found != targets.failures.end() && found->first == failure
            ? found->second
            : targets.failure_fallback;
    }

    auto set_failure_target(
        AvailabilityTargets& targets,
        HIRTypeID failure,
        AvailabilityBlockID target
    ) const noexcept -> void {
        const auto found = std::ranges::lower_bound(
            targets.failures,
            failure.index(),
            {},
            [](const auto& entry) static noexcept { return entry.first.index(); }
        );
        if (found != targets.failures.end() && found->first == failure) {
            found->second = target;
        } else {
            targets.failures.insert(found, {failure, target});
        }
    }

    auto failure_branch(
        std::span<const HIRTypeID> failures,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        auto successors = std::vector<AvailabilityBlockID> {normal};
        for (const auto failure : failures) {
            successors.push_back(failure_target(failure, targets));
        }
        return branch(std::move(successors));
    }

    auto call_failures(HIRExprID call) const noexcept -> std::span<const HIRTypeID> {
        return std::visit(
            Overloaded {
                [&](const ConcreteCallableFailure& source) noexcept -> std::span<const HIRTypeID> {
                    return hir.failure_set(hir.callable_flow(source.callable).effective_failure_set)
                        .members;
                },
                [&](const FixedSignatureFailure& source) noexcept -> std::span<const HIRTypeID> {
                    return hir.failure_set(source.failure_set).members;
                },
                [](const ForeignCallableFailure&) static noexcept -> std::span<const HIRTypeID> {
                    return {};
                },
            },
            call_contract(hir, call).failure_source
        );
    }

    auto add_locals(
        std::vector<AvailabilityPlaceID>& destination,
        std::span<const SymbolID> places
    ) const noexcept -> void {
        for (const auto place : places) {
            destination.push_back(local(place));
        }
        std::ranges::sort(destination, {}, &AvailabilityPlaceID::value);
        destination.erase(std::ranges::unique(destination).begin(), destination.end());
    }

    auto remove_local(
        std::vector<AvailabilityPlaceID>& places,
        AvailabilityPlaceID place
    ) const noexcept -> void {
        std::erase(places, place);
    }

    auto call_check(HIRExprID id, const HIRCallExpr& call) const noexcept
        -> AvailabilityOperationCheck {
        auto result = AvailabilityOperationCheck {
            .primary = id,
            .related = std::nullopt,
            .reads = {},
            .writes = {},
            .takes = {},
        };
        const auto add_effect = [&](HIRExprID expression) noexcept {
            const auto& effect = hir.evaluation_effect(expression);
            add_locals(result.reads, effect.reads);
            add_locals(result.reads, effect.writes);
            add_locals(result.takes, effect.takes);
        };
        add_effect(call.callee);
        for (const auto& argument : call.arguments) {
            add_effect(argument.expression);
            if (argument.access != HIRAccessMode::Write) {
                continue;
            }
            const auto& use = hir.place_use(argument.expression);
            if (!use.has_value()) {
                continue;
            }
            const auto place = local(use->root);
            remove_local(result.reads, place);
            result.writes.push_back(place);
        }
        std::ranges::sort(result.writes, {}, &AvailabilityPlaceID::value);
        result.writes.erase(std::ranges::unique(result.writes).begin(), result.writes.end());
        return result;
    }

    auto assignment_checks(const HIRAssignmentStmt& assignment) const noexcept
        -> std::array<AvailabilityOperationCheck, 2> {
        auto target_uses = std::vector<AvailabilityPlaceID>();
        const auto& target_effect = hir.evaluation_effect(assignment.target);
        add_locals(target_uses, target_effect.reads);
        add_locals(target_uses, target_effect.writes);
        auto source_uses = std::vector<AvailabilityPlaceID>();
        auto source_takes = std::vector<AvailabilityPlaceID>();
        const auto& source_effect = hir.evaluation_effect(assignment.value);
        add_locals(source_uses, source_effect.reads);
        add_locals(source_uses, source_effect.writes);
        add_locals(source_takes, source_effect.takes);
        return {
            AvailabilityOperationCheck {
                .primary = assignment.value,
                .related = assignment.target,
                .reads = assignment.op == HIRAssignmentOperator::Assign
                    ? std::vector<AvailabilityPlaceID>()
                    : target_uses,
                .writes = std::move(target_uses),
                .takes = source_takes,
            },
            AvailabilityOperationCheck {
                .primary = assignment.value,
                .related = std::nullopt,
                .reads = std::move(source_uses),
                .writes = {},
                .takes = std::move(source_takes),
            },
        };
    }

    auto append_access(HIRExprID id, AvailabilityBlockID next) noexcept -> AvailabilityBlockID {
        const auto& use = hir.place_use(id);
        if (!use.has_value()) {
            return next;
        }
        const auto place = local(use->root);
        if (use->access != SemanticPlaceAccess::Take) {
            return prepend(AvailabilityUse {.expression = id, .place = place}, next);
        }
        const auto& source = hir.expression(id).value;
        const auto origin = [&]() noexcept {
            if (const auto* take = std::get_if<HIRTakeExpr>(&source)) {
                return take->marker_origin;
            }
            return hir.expression(id).origin;
        }();
        if (!use->projections.empty()) {
            return prepend(
                AvailabilityInvalidTake {
                    .origin = origin,
                    .kind = InvalidTakeKind::Partial,
                },
                next
            );
        }
        const auto& binding = hir.binding(use->root);
        if (!binding.has_value() || binding->storage != SemanticBindingStorage::Owner) {
            return prepend(
                AvailabilityInvalidTake {
                    .origin = origin,
                    .kind = InvalidTakeKind::NonOwner,
                },
                next
            );
        }
        const auto span = diagnostic_span(hir, origin);
        return prepend(
            AvailabilityTake {
                .expression = id,
                .place = place,
                .site =
                    TakeSiteID {
                        .origin = origin,
                        .source = span.source_id.index(),
                        .offset = span.span.start(),
                        .expression = id.index(),
                    },
            },
            next
        );
    }

    auto pattern_places(
        HIRPatternID pattern,
        std::vector<AvailabilityPlaceID>& places
    ) const noexcept -> void {
        std::visit(
            Overloaded {
                [&](const HIRBindingPattern& binding) noexcept {
                    places.push_back(required_place(binding.target.symbol));
                },
                [&](const HIROrPattern& alternatives) noexcept {
                    for (const auto child : alternatives.alternatives) {
                        pattern_places(child, places);
                    }
                },
                [&](const HIRCasePattern& value) noexcept {
                    for (const auto child : value.payload) {
                        pattern_places(child, places);
                    }
                },
                [](const auto&) static noexcept {},
            },
            hir.pattern(pattern).value
        );
    }

    auto restore_patterns(std::span<const HIRPatternID> patterns, AvailabilityBlockID next) noexcept
        -> AvailabilityBlockID {
        auto places = std::vector<AvailabilityPlaceID>();
        for (const auto pattern : patterns) {
            pattern_places(pattern, places);
        }
        std::ranges::sort(places, {}, &AvailabilityPlaceID::value);
        places.erase(std::ranges::unique(places).begin(), places.end());
        for (auto position = places.rbegin(); position != places.rend(); ++position) {
            next = prepend(
                AvailabilityRestore {
                    .place = *position,
                    .requires_write = false,
                },
                next
            );
        }
        return next;
    }

    auto lower_conditional(
        std::span<const HIRConditionalBranch> branches,
        std::optional<HIRBlockID> other,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        auto fallback = other.has_value() ? lower_block(*other, normal, targets) : normal;
        for (auto position = branches.rbegin(); position != branches.rend(); ++position) {
            const auto taken = lower_block(position->body, normal, targets);
            const auto decision = branch({taken, fallback});
            fallback = lower_expression(position->condition, decision, targets);
        }
        return fallback;
    }

    auto lower_match(
        HIRExprID subject,
        std::span<const HIRMatchArm> arms,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        auto entries = std::vector<AvailabilityBlockID>(arms.size(), normal);
        auto fallback = normal;
        for (auto index = arms.size(); index > 0; --index) {
            const auto& arm = arms[index - 1];
            auto entry = lower_block(arm.body, normal, targets);
            if (arm.guard.has_value()) {
                entry = lower_expression(*arm.guard, branch({entry, fallback}), targets);
            }
            const auto patterns = std::array {arm.pattern};
            entry = restore_patterns(patterns, entry);
            entries[index - 1] = entry;
            fallback = entry;
        }
        const auto dispatch = entries.empty() ? normal : branch(std::move(entries));
        return lower_expression(subject, dispatch, targets);
    }

    auto lower_try(
        HIRExprID id,
        const HIRTryExpr& attempt,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        const auto& facts = hir.try_facts(id);
        if (!facts.has_value() || facts->arms.size() != attempt.arms.size()) {
            invariant_violation("availability requires aligned frozen try facts");
        }
        const auto accepted_failures = [&](std::size_t arm) noexcept -> std::span<const HIRTypeID> {
            return hir.failure_set(facts->arms[arm].accepted_failure_set).members;
        };
        const auto& unhandled = hir.failure_set(facts->unhandled_failure_set).members;
        auto handler_entries = std::vector<AvailabilityBlockID>(attempt.arms.size(), normal);
        for (auto index = attempt.arms.size(); index > 0; --index) {
            const auto arm_index = index - 1;
            const auto& arm = attempt.arms[arm_index];
            const auto accepted = accepted_failures(arm_index);
            auto handler_targets = targets;
            handler_targets.rethrows.assign(accepted.begin(), accepted.end());
            auto entry = lower_block(arm.body, normal, handler_targets);
            if (arm.guard.has_value()) {
                auto fallback = std::vector<AvailabilityBlockID>();
                for (auto later = arm_index + 1; later < attempt.arms.size(); ++later) {
                    const auto later_failures = accepted_failures(later);
                    const auto overlaps = std::ranges::any_of(accepted, [&](HIRTypeID failure) {
                        return std::ranges::contains(later_failures, failure);
                    });
                    if (overlaps) {
                        fallback.push_back(handler_entries[later]);
                    }
                }
                for (const auto failure : accepted) {
                    if (std::ranges::contains(unhandled, failure)) {
                        fallback.push_back(failure_target(failure, targets));
                    }
                }
                if (fallback.empty()) {
                    fallback.push_back(targets.failure_fallback);
                }
                fallback.push_back(entry);
                entry = lower_expression(*arm.guard, branch(std::move(fallback)), handler_targets);
            }
            auto patterns = std::vector<HIRPatternID>();
            for (const auto& alternative : arm.alternatives) {
                if (alternative.inner.has_value()) {
                    patterns.push_back(*alternative.inner);
                }
            }
            entry = restore_patterns(patterns, entry);
            handler_entries[arm_index] = entry;
        }

        auto protected_targets = targets;
        const auto& protected_failures =
            hir.failure_set(hir.block_control(attempt.body).outward_failure_set).members;
        for (const auto failure : protected_failures) {
            auto successors = std::vector<AvailabilityBlockID>();
            for (auto arm = 0uz; arm < attempt.arms.size(); ++arm) {
                if (std::ranges::contains(accepted_failures(arm), failure)) {
                    successors.push_back(handler_entries[arm]);
                }
            }
            if (std::ranges::contains(unhandled, failure)) {
                successors.push_back(failure_target(failure, targets));
            }
            if (successors.empty()) {
                successors.push_back(failure_target(failure, targets));
            }
            set_failure_target(protected_targets, failure, branch(std::move(successors)));
        }
        return lower_block(attempt.body, normal, protected_targets);
    }

    auto lower_expression(
        HIRExprID id,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        const auto after_access = append_access(id, normal);
        return std::visit(
            Overloaded {
                [&](const HIRLiteralExpr&) noexcept { return after_access; },
                [&](const HIRNameExpr&) noexcept { return after_access; },
                [&](const HIRArrayExpr& value) noexcept {
                    auto entry = after_access;
                    for (auto child = value.element_ids.rbegin(); child != value.element_ids.rend();
                         ++child) {
                        entry = lower_expression(*child, entry, targets);
                    }
                    return entry;
                },
                [&](const HIRConstructionExpr& value) noexcept {
                    auto entry = after_access;
                    for (auto field = value.fields.rbegin(); field != value.fields.rend();
                         ++field) {
                        entry = lower_expression(field->value, entry, targets);
                    }
                    return entry;
                },
                [&](const HIRCaseConstructionExpr& value) noexcept {
                    auto entry = after_access;
                    for (auto child = value.payload.rbegin(); child != value.payload.rend();
                         ++child) {
                        entry = lower_expression(*child, entry, targets);
                    }
                    return entry;
                },
                [&](const HIRUnaryExpr& value) noexcept {
                    return lower_expression(value.operand_id, after_access, targets);
                },
                [&](const HIRBinaryExpr& value) noexcept {
                    const auto right = lower_expression(value.right, after_access, targets);
                    const auto right_target = value.op == HIRBinaryExpr::Operator::LogicalAnd
                            || value.op == HIRBinaryExpr::Operator::LogicalOr
                        ? branch({after_access, right})
                        : right;
                    return lower_expression(value.left, right_target, targets);
                },
                [&](const HIRCastExpr& value) noexcept {
                    return lower_expression(value.operand_id, after_access, targets);
                },
                [&](const HIRCallExpr& value) noexcept {
                    auto entry = failure_branch(call_failures(id), after_access, targets);
                    for (auto argument = value.arguments.rbegin();
                         argument != value.arguments.rend();
                         ++argument) {
                        entry = lower_expression(argument->expression, entry, targets);
                    }
                    entry = lower_expression(value.callee, entry, targets);
                    return prepend(call_check(id, value), entry);
                },
                [&](const HIRClosureExpr& value) noexcept {
                    auto entry = after_access;
                    for (auto capture = value.captures.rbegin(); capture != value.captures.rend();
                         ++capture) {
                        entry = prepend(
                            AvailabilityCapture {
                                .expression = id,
                                .source = required_place(capture->source),
                                .origin = capture->origin,
                                .write = capture->mode == HIRCaptureMode::Write,
                            },
                            entry
                        );
                    }
                    return entry;
                },
                [&](const HIRCallableViewExpr& value) noexcept {
                    return lower_expression(value.source, after_access, targets);
                },
                [&](const HIRPropagationExpr& value) noexcept {
                    return lower_expression(value.operand_id, after_access, targets);
                },
                [&](const HIRTakeExpr& value) noexcept {
                    if (whole_place(id).has_value()) {
                        return after_access;
                    }
                    return lower_expression(value.operand_id, after_access, targets);
                },
                [&](const HIRTextIntrinsicExpr& value) noexcept {
                    return lower_expression(value.operand_id, after_access, targets);
                },
                [&](const HIRIndexExpr& value) noexcept {
                    const auto entry = lower_expression(value.index, after_access, targets);
                    return lower_expression(value.operand_id, entry, targets);
                },
                [&](const HIRMemberExpr& value) noexcept {
                    return lower_expression(value.operand_id, after_access, targets);
                },
                [&](const HIRIfExpr& value) noexcept {
                    return lower_conditional(
                        value.branches,
                        value.else_branch,
                        after_access,
                        targets
                    );
                },
                [&](const HIRMatchExpr& value) noexcept {
                    return lower_match(value.subject, value.arms, after_access, targets);
                },
                [&](const HIRTryExpr& value) noexcept {
                    return lower_try(id, value, after_access, targets);
                },
                [&](const HIRCppExpr&) noexcept { return after_access; },
            },
            hir.expression(id).value
        );
    }

    auto lower_while(
        const HIRWhileStmt& loop,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        const auto header = graph.append();
        auto loop_targets = targets;
        loop_targets.broken = normal;
        loop_targets.continued = header;
        const auto body_entry = lower_block(loop.body, header, loop_targets);
        const auto decision = branch({normal, body_entry});
        const auto condition = lower_expression(loop.condition, decision, targets);
        graph.set_successors(header, {condition});
        return header;
    }

    auto lower_c_style_for(
        const HIRCStyleForStmt& loop,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        const auto header = graph.append();
        auto steps = header;
        for (auto position = loop.steps.rbegin(); position != loop.steps.rend(); ++position) {
            steps = lower_statement(*position, steps, targets);
        }
        auto loop_targets = targets;
        loop_targets.broken = normal;
        loop_targets.continued = steps;
        const auto body_entry = lower_block(loop.body, steps, loop_targets);
        const auto decision = loop.condition.has_value()
            ? lower_expression(*loop.condition, branch({normal, body_entry}), targets)
            : body_entry;
        graph.set_successors(header, {decision});
        auto entry = header;
        if (loop.initializer.has_value()) {
            entry = lower_statement(*loop.initializer, entry, targets);
        }
        return entry;
    }

    auto lower_range_for(
        const HIRRangeForStmt& loop,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        const auto header = graph.append();
        auto loop_targets = targets;
        loop_targets.broken = normal;
        loop_targets.continued = header;
        auto body_entry = lower_block(loop.body, header, loop_targets);
        if (const auto* named = std::get_if<HIRNamedBindingTarget>(&loop.target)) {
            body_entry = prepend(
                AvailabilityRestore {
                    .place = required_place(named->symbol),
                    .requires_write = false,
                },
                body_entry
            );
        }
        graph.set_successors(header, {normal, body_entry});
        return std::visit(
            Overloaded {
                [&](HIRExprID expression) noexcept {
                    return lower_expression(expression, header, targets);
                },
                [&](const HIRHalfOpenRange& range) noexcept {
                    const auto entry = lower_expression(range.end, header, targets);
                    return lower_expression(range.begin, entry, targets);
                },
            },
            loop.iterable
        );
    }

    auto lower_statement(
        HIRStmtID id,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        return std::visit(
            Overloaded {
                [&](const HIRReturnStmt& value) noexcept {
                    return value.value.has_value()
                        ? lower_expression(*value.value, targets.returned, targets)
                        : targets.returned;
                },
                [&](const HIRBreakStmt&) noexcept { return targets.broken; },
                [&](const HIRContinueStmt&) noexcept { return targets.continued; },
                [&](const HIRThrowStmt& value) noexcept {
                    return lower_expression(
                        value.value,
                        failure_target(value.failure_type, targets),
                        targets
                    );
                },
                [&](const HIRRethrowStmt&) noexcept {
                    auto successors = std::vector<AvailabilityBlockID>();
                    for (const auto failure : targets.rethrows) {
                        successors.push_back(failure_target(failure, targets));
                    }
                    return successors.empty() ? targets.failure_fallback
                                              : branch(std::move(successors));
                },
                [&](const HIRExprStmt& value) noexcept {
                    return lower_expression(value.expression, normal, targets);
                },
                [&](const HIRBindingStmt& value) noexcept {
                    auto after = normal;
                    if (const auto* named = std::get_if<HIRNamedBindingTarget>(&value.target)) {
                        after = prepend(
                            AvailabilityRestore {
                                .place = required_place(named->symbol),
                                .requires_write = false,
                            },
                            after
                        );
                    }
                    return lower_expression(value.initializer, after, targets);
                },
                [&](const HIRAssignmentStmt& value) noexcept {
                    const auto owner = whole_place(value.target);
                    const auto direct =
                        value.op == HIRAssignmentOperator::Assign && owner.has_value();
                    auto after = normal;
                    if (direct) {
                        after = prepend(
                            AvailabilityRestore {
                                .place = local(*owner),
                                .requires_write = true,
                            },
                            after
                        );
                    }
                    auto entry = lower_expression(value.value, after, targets);
                    if (!direct) {
                        entry = lower_expression(value.target, entry, targets);
                    }
                    auto checks = assignment_checks(value);
                    entry = prepend(std::move(checks[1]), entry);
                    return prepend(std::move(checks[0]), entry);
                },
                [&](const HIRUpdateStmt& value) noexcept {
                    return lower_expression(value.target, normal, targets);
                },
                [&](const HIRIfStmt& value) noexcept {
                    return lower_conditional(value.branches, value.else_branch, normal, targets);
                },
                [&](const HIRMatchStmt& value) noexcept {
                    return lower_match(value.subject, value.arms, normal, targets);
                },
                [&](const HIRWhileStmt& value) noexcept {
                    return lower_while(value, normal, targets);
                },
                [&](const HIRCStyleForStmt& value) noexcept {
                    return lower_c_style_for(value, normal, targets);
                },
                [&](const HIRRangeForStmt& value) noexcept {
                    return lower_range_for(value, normal, targets);
                },
                [&](const HIRTestCheckStmt& value) noexcept {
                    auto entry = normal;
                    if (value.message.has_value()) {
                        entry = lower_expression(*value.message, entry, targets);
                    }
                    return lower_expression(value.condition, entry, targets);
                },
                [&](const HIRTestRequireStmt& value) noexcept {
                    auto entry = branch({normal, targets.test_exit});
                    if (value.message.has_value()) {
                        entry = lower_expression(*value.message, entry, targets);
                    }
                    return lower_expression(value.condition, entry, targets);
                },
                [&](const HIRTestFailStmt& value) noexcept {
                    return value.message.has_value()
                        ? lower_expression(*value.message, targets.test_exit, targets)
                        : targets.test_exit;
                },
                [&](const HIRCppStmt&) noexcept { return normal; },
            },
            hir.statement(id).value
        );
    }

    auto lower_block(
        HIRBlockID id,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID {
        const auto& block = hir.block(id);
        auto entry =
            block.result.has_value() ? lower_expression(*block.result, normal, targets) : normal;
        for (auto position = block.statements.rbegin(); position != block.statements.rend();
             ++position) {
            entry = lower_statement(*position, entry, targets);
        }
        return entry;
    }

    SemanticDraftView hir;
    const AvailabilityPlaceCatalog& catalog;
    BodyID body;
    MutableAvailabilityGraph graph;
};

class UnavailableBits final {
public:
    explicit UnavailableBits(std::size_t places = 0) noexcept
        : many(places > 64 ? (places + 63) / 64 : 0, 0) {}

    auto contains(AvailabilityPlaceID place) const noexcept -> bool {
        const auto mask = 1ull << (place.value % 64);
        return many.empty() ? (one & mask) != 0 : (many[place.value / 64] & mask) != 0;
    }

    auto insert(AvailabilityPlaceID place) noexcept -> bool {
        const auto mask = 1ull << (place.value % 64);
        auto& word = many.empty() ? one : many[place.value / 64];
        const auto changed = (word & mask) == 0;
        word |= mask;
        return changed;
    }

    auto erase(AvailabilityPlaceID place) noexcept -> bool {
        const auto mask = 1ull << (place.value % 64);
        auto& word = many.empty() ? one : many[place.value / 64];
        const auto changed = (word & mask) != 0;
        word &= ~mask;
        return changed;
    }

    auto join(const UnavailableBits& source) noexcept -> bool {
        if (many.size() != source.many.size()) {
            invariant_violation("availability bitsets have incompatible storage widths");
        }
        auto changed = false;
        if (many.empty()) {
            const auto previous = one;
            one |= source.one;
            return one != previous;
        }
        for (auto&& [destination, incoming] : std::views::zip(many, source.many)) {
            const auto previous = destination;
            destination |= incoming;
            changed |= destination != previous;
        }
        return changed;
    }

private:
    std::uint64_t one = 0;
    std::vector<std::uint64_t> many;
};

struct AvailabilityWitness final {
    AvailabilityPlaceID place;
    TakeSiteID site;
    constexpr auto operator==(const AvailabilityWitness&) const noexcept -> bool = default;
};

class AvailabilityState final {
public:
    explicit AvailabilityState(std::size_t place_count = 0) noexcept
        : unavailable(place_count) {}

    auto contains(AvailabilityPlaceID place) const noexcept -> bool {
        return unavailable.contains(place);
    }

    auto witness(AvailabilityPlaceID place) const noexcept -> TakeSiteID {
        const auto found = std::ranges::lower_bound(
            witnesses,
            place.value,
            {},
            [](const AvailabilityWitness& value) static noexcept { return value.place.value; }
        );
        if (found == witnesses.end() || found->place != place) {
            invariant_violation("unavailable place has no provenance witness");
        }
        return found->site;
    }

    auto take(AvailabilityPlaceID place, TakeSiteID site) noexcept -> bool {
        if (!unavailable.insert(place)) {
            return false;
        }
        const auto found = std::ranges::lower_bound(
            witnesses,
            place.value,
            {},
            [](const AvailabilityWitness& value) static noexcept { return value.place.value; }
        );
        witnesses.insert(found, {.place = place, .site = site});
        return true;
    }

    auto restore(AvailabilityPlaceID place) noexcept -> void {
        if (!unavailable.erase(place)) {
            return;
        }
        const auto found = std::ranges::lower_bound(
            witnesses,
            place.value,
            {},
            [](const AvailabilityWitness& value) static noexcept { return value.place.value; }
        );
        if (found == witnesses.end() || found->place != place) {
            invariant_violation("restored unavailable place has no provenance witness");
        }
        witnesses.erase(found);
    }

    auto join(const AvailabilityState& source) noexcept -> bool {
        auto changed = unavailable.join(source.unavailable);
        auto merged = std::vector<AvailabilityWitness>();
        merged.reserve(witnesses.size() + source.witnesses.size());
        auto left = 0uz;
        auto right = 0uz;
        while (left < witnesses.size() && right < source.witnesses.size()) {
            const auto left_place = witnesses[left].place.value;
            const auto right_place = source.witnesses[right].place.value;
            if (left_place < right_place) {
                merged.push_back(witnesses[left++]);
            } else if (right_place < left_place) {
                merged.push_back(source.witnesses[right++]);
            } else {
                merged.push_back(
                    witnesses[left].site <= source.witnesses[right].site ? witnesses[left]
                                                                         : source.witnesses[right]
                );
                ++left;
                ++right;
            }
        }
        merged.insert(
            merged.end(),
            witnesses.begin() + static_cast<std::ptrdiff_t>(left),
            witnesses.end()
        );
        merged.insert(
            merged.end(),
            source.witnesses.begin() + static_cast<std::ptrdiff_t>(right),
            source.witnesses.end()
        );
        changed |= merged != witnesses;
        witnesses = std::move(merged);
        return changed;
    }

private:
    UnavailableBits unavailable;
    std::vector<AvailabilityWitness> witnesses;
};

class BodyAvailabilitySolver final {
public:
    BodyAvailabilitySolver(
        SemanticDraftView hir,
        DiagnosticSink& diagnostics,
        const AvailabilityPlaceCatalog& catalog,
        BodyID body,
        const BodyAvailabilityGraph& graph
    ) noexcept
        : hir(hir),
          diagnostics(diagnostics),
          catalog(catalog),
          body(body),
          graph(graph) {}

    auto run() noexcept -> void {
        auto entries = std::vector<std::optional<AvailabilityState>>(graph.blocks.size());
        auto queued = std::vector<std::uint8_t>(graph.blocks.size(), 0);
        auto worklist = std::deque<AvailabilityBlockID>();
        entries[graph.entry.value] = AvailabilityState(catalog.count(body));
        queued[graph.entry.value] = 1;
        worklist.push_back(graph.entry);
        while (!worklist.empty()) {
            const auto block_id = worklist.front();
            worklist.pop_front();
            queued[block_id.value] = 0;
            auto state = *entries[block_id.value];
            const auto& block = graph.blocks[block_id.value];
            for (auto offset = 0u; offset < block.operation_count; ++offset) {
                execute(graph.operations[block.operation_begin + offset], state);
            }
            for (auto offset = 0u; offset < block.successor_count; ++offset) {
                const auto successor = graph.successors[block.successor_begin + offset];
                auto changed = false;
                if (!entries[successor.value].has_value()) {
                    entries[successor.value] = state;
                    changed = true;
                } else {
                    changed = entries[successor.value]->join(state);
                }
                if (changed && queued[successor.value] == 0) {
                    queued[successor.value] = 1;
                    worklist.push_back(successor);
                }
            }
        }
        publish_diagnostics();
    }

private:
    auto locate(ProgramOriginID origin) const noexcept -> SourceSpan {
        return diagnostic_span(hir, origin);
    }

    auto take_origin(TakeSiteID site) const noexcept -> ProgramOriginID { return site.origin; }

    auto remember_unavailable(
        HIRExprID expression,
        AvailabilityPlaceID place,
        TakeSiteID site
    ) noexcept -> void {
        const auto key = std::pair(expression, place.value);
        const auto found = unavailable.find(key);
        if (found == unavailable.end()) {
            unavailable.emplace(key, site);
        } else if (site < found->second) {
            found->second = site;
        }
    }

    auto execute(const AvailabilityOperation& operation, AvailabilityState& state) noexcept
        -> void {
        std::visit(
            Overloaded {
                [&](const AvailabilityUse& value) noexcept {
                    if (state.contains(value.place)) {
                        remember_unavailable(
                            value.expression,
                            value.place,
                            state.witness(value.place)
                        );
                    }
                },
                [&](const AvailabilityTake& value) noexcept {
                    const auto found = reached_takes.find(value.place.value);
                    if (found == reached_takes.end()) {
                        reached_takes.emplace(value.place.value, value.site);
                    } else if (value.site < found->second) {
                        found->second = value.site;
                    }
                    if (state.contains(value.place)) {
                        remember_unavailable(
                            value.expression,
                            value.place,
                            state.witness(value.place)
                        );
                    } else {
                        static_cast<void>(state.take(value.place, value.site));
                    }
                },
                [&](const AvailabilityRestore& value) noexcept {
                    const auto symbol = catalog.global(body, value.place);
                    const auto& binding = hir.binding(symbol);
                    if (!binding.has_value()) {
                        invariant_violation("availability restore references a non-binding symbol");
                    }
                    if (!value.requires_write || binding->capabilities.write) {
                        state.restore(value.place);
                    }
                },
                [&](const AvailabilityCapture& value) noexcept {
                    if (state.contains(value.source)) {
                        remember_unavailable(
                            value.expression,
                            value.source,
                            state.witness(value.source)
                        );
                    }
                    if (!value.write) {
                        return;
                    }
                    const auto found = reached_write_captures.find(value.source.value);
                    if (found == reached_write_captures.end()) {
                        reached_write_captures.emplace(value.source.value, value.origin);
                    } else if (value.origin.index() < found->second.index()) {
                        found->second = value.origin;
                    }
                },
                [&](const AvailabilityOperationCheck& value) noexcept {
                    for (const auto place : value.takes) {
                        if (!std::ranges::contains(value.reads, place)
                            && !std::ranges::contains(value.writes, place)) {
                            continue;
                        }
                        operation_conflicts.try_emplace(
                            {value.primary, place.value},
                            value.related
                        );
                    }
                },
                [&](const AvailabilityInvalidTake& value) noexcept {
                    invalid_takes.insert({value.origin, value.kind});
                },
            },
            operation
        );
    }

    auto publish_diagnostics() noexcept -> void {
        for (const auto& [key, site] : unavailable) {
            const auto [expression, local_place] = key;
            diagnostics.emit(
                DiagnosticBuilder(
                    DiagnosticCode::AccessUnavailable,
                    "binding is unavailable after Take"
                )
                    .primary(locate(hir.expression(expression).origin), "unavailable use")
                    .related(locate(take_origin(site)), "binding became unavailable here")
                    .build()
            );
        }
        for (const auto& [key, related] : operation_conflicts) {
            const auto [primary, local_place] = key;
            auto diagnostic = DiagnosticBuilder(
                                  DiagnosticCode::AccessOperationConflict,
                                  "one operation cannot both access and Take the same binding"
            )
                                  .primary(locate(hir.expression(primary).origin));
            if (related.has_value()) {
                diagnostic.related(
                    locate(hir.expression(*related).origin),
                    "the assignment target accesses the same binding"
                );
            }
            diagnostics.emit(std::move(diagnostic).build());
        }
        for (const auto& [origin, kind] : invalid_takes) {
            const auto* message = kind == InvalidTakeKind::Partial
                ? "partial Take from a member or element is not supported"
                : "Take requires a whole runtime owner binding or a temporary";
            diagnostics.emit(DiagnosticBuilder(DiagnosticCode::AccessTakeOperand, message)
                                 .primary(locate(origin))
                                 .build());
        }
        for (const auto& [place, capture_origin] : reached_write_captures) {
            const auto taken = reached_takes.find(place);
            if (taken == reached_takes.end()) {
                continue;
            }
            diagnostics.emit(
                DiagnosticBuilder(
                    DiagnosticCode::AccessCaptureConflict,
                    "binding cannot be both a Write capture source and a Take source in one "
                    "callable"
                )
                    .primary(locate(take_origin(taken->second)), "Take source")
                    .related(locate(capture_origin), "Write capture source")
                    .build()
            );
        }
    }

    SemanticDraftView hir;
    DiagnosticSink& diagnostics;
    const AvailabilityPlaceCatalog& catalog;
    BodyID body;
    const BodyAvailabilityGraph& graph;
    std::flat_map<std::pair<HIRExprID, std::uint32_t>, TakeSiteID> unavailable;
    std::flat_map<std::pair<HIRExprID, std::uint32_t>, std::optional<HIRExprID>>
        operation_conflicts;
    std::flat_set<std::pair<ProgramOriginID, InvalidTakeKind>> invalid_takes;
    std::flat_map<std::uint32_t, TakeSiteID> reached_takes;
    std::flat_map<std::uint32_t, ProgramOriginID> reached_write_captures;
};

auto diagnose_body(
    SemanticDraftView hir,
    DiagnosticSink& diagnostics,
    const AvailabilityPlaceCatalog& catalog,
    BodyID body
) noexcept -> void {
    const auto graph = BodyAvailabilityGraphBuilder(hir, catalog, body).build();
    BodyAvailabilitySolver(hir, diagnostics, catalog, body, graph).run();
}

} // namespace

auto diagnose_availability(SemanticDraftView builder, DiagnosticSink& diagnostics) noexcept
    -> void {
    const auto catalog = AvailabilityPlaceCatalog(builder);
    for (auto index = 0uz; index < builder.callables().size(); ++index) {
        const auto callable = CallableID::from_index(static_cast<std::uint32_t>(index));
        diagnose_body(builder, diagnostics, catalog, builder.callable(callable).body);
    }
    for (auto index = 0uz; index < builder.tests().size(); ++index) {
        const auto test = TestID::from_index(static_cast<std::uint32_t>(index));
        diagnose_body(builder, diagnostics, catalog, builder.test(test).body);
    }
}
