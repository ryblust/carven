module carven:semantic.analysis.availability.build.impl;

import :semantic.analysis.availability.build;
import :semantic.analysis.availability.graph;
import :semantic.analysis.availability.place;
import :semantic.analysis.analyzer;
import :semantic.analysis.call_contract;
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

BodyAvailabilityGraphBuilder::BodyAvailabilityGraphBuilder(
    SemanticDraftView hir,
    const AvailabilityPlaceCatalog& catalog,
    BodyID body
) noexcept
    : hir(hir),
      catalog(catalog),
      body(body) {}

auto BodyAvailabilityGraphBuilder::build() && noexcept -> BodyAvailabilityGraph {
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

auto BodyAvailabilityGraphBuilder::local(SymbolID symbol) const noexcept -> AvailabilityPlaceID {
    return catalog.local(body, symbol);
}

auto BodyAvailabilityGraphBuilder::required_place(SymbolID symbol) const noexcept
    -> AvailabilityPlaceID {
    if (!hir.binding(symbol).has_value()) {
        invariant_violation("runtime binding has no published semantic facts");
    }
    return local(symbol);
}

auto BodyAvailabilityGraphBuilder::whole_place(HIRExprID expression) const noexcept
    -> std::optional<SymbolID> {
    const auto& use = hir.place_use(expression);
    return use.has_value() && use->projections.empty() ? std::optional(use->root) : std::nullopt;
}

auto BodyAvailabilityGraphBuilder::prepend(
    AvailabilityOperation operation,
    AvailabilityBlockID next
) noexcept -> AvailabilityBlockID {
    return graph.append({std::move(operation)}, {next});
}

auto BodyAvailabilityGraphBuilder::branch(std::vector<AvailabilityBlockID> successors) noexcept
    -> AvailabilityBlockID {
    std::ranges::sort(successors, {}, &AvailabilityBlockID::value);
    successors.erase(std::ranges::unique(successors).begin(), successors.end());
    if (successors.size() == 1) {
        return successors.front();
    }
    return graph.append({}, std::move(successors));
}

auto BodyAvailabilityGraphBuilder::failure_target(
    HIRTypeID failure,
    const AvailabilityTargets& targets
) const noexcept -> AvailabilityBlockID {
    const auto found = std::ranges::lower_bound(
        targets.failures,
        failure.index(),
        {},
        [](const auto& entry) static noexcept { return entry.first.index(); }
    );
    return found != targets.failures.end() && found->first == failure ? found->second
                                                                      : targets.failure_fallback;
}

auto BodyAvailabilityGraphBuilder::set_failure_target(
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

auto BodyAvailabilityGraphBuilder::failure_branch(
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

auto BodyAvailabilityGraphBuilder::call_failures(HIRExprID call) const noexcept
    -> std::span<const HIRTypeID> {
    return std::visit(
        Overloaded {
            [&](const ConcreteCallableFailure& source) noexcept -> std::span<const HIRTypeID> {
                return hir.failure_set(hir.callable_flow(source.callable).effective_failure_set)
                    .members;
            },
            [&](const FixedSignatureFailure& source) noexcept -> std::span<const HIRTypeID> {
                return hir.failure_set(source.failure_set).members;
            },
        },
        call_contract(hir, call).failure_source
    );
}

auto BodyAvailabilityGraphBuilder::add_locals(
    std::vector<AvailabilityPlaceID>& destination,
    std::span<const SymbolID> places
) const noexcept -> void {
    for (const auto place : places) {
        destination.push_back(local(place));
    }
    std::ranges::sort(destination, {}, &AvailabilityPlaceID::value);
    destination.erase(std::ranges::unique(destination).begin(), destination.end());
}

auto BodyAvailabilityGraphBuilder::remove_local(
    std::vector<AvailabilityPlaceID>& places,
    AvailabilityPlaceID place
) const noexcept -> void {
    std::erase(places, place);
}

auto BodyAvailabilityGraphBuilder::call_check(HIRExprID id, const HIRCallExpr& call) const noexcept
    -> AvailabilityAccessCheck {
    auto result = AvailabilityAccessCheck {
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

auto BodyAvailabilityGraphBuilder::assignment_checks(
    const HIRAssignmentStmt& assignment
) const noexcept -> std::array<AvailabilityAccessCheck, 2> {
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
        AvailabilityAccessCheck {
            .primary = assignment.value,
            .related = assignment.target,
            .reads = assignment.op == HIRAssignmentOperator::Assign
                ? std::vector<AvailabilityPlaceID>()
                : target_uses,
            .writes = std::move(target_uses),
            .takes = source_takes,
        },
        AvailabilityAccessCheck {
            .primary = assignment.value,
            .related = std::nullopt,
            .reads = std::move(source_uses),
            .writes = {},
            .takes = std::move(source_takes),
        },
    };
}

auto BodyAvailabilityGraphBuilder::append_access(HIRExprID id, AvailabilityBlockID next) noexcept
    -> AvailabilityBlockID {
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
                TakeSite {
                    .origin = origin,
                    .source = span.source_id.index(),
                    .offset = span.span.start(),
                    .expression = id.index(),
                },
        },
        next
    );
}

auto BodyAvailabilityGraphBuilder::pattern_places(
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

auto BodyAvailabilityGraphBuilder::restore_patterns(
    std::span<const HIRPatternID> patterns,
    AvailabilityBlockID next
) noexcept -> AvailabilityBlockID {
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
