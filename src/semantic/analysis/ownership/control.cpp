module carven:semantic.analysis.ownership.control.impl;

import :semantic.analysis.ownership.context;
import :semantic.analysis.pattern.control;
import std;

auto OwnershipBodyAnalyzer::conditional(const SemIf& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    auto remaining = std::optional(OwnershipNormal {std::move(state), {}, {}});
    auto result = OwnershipFlow {};
    for (const auto& branch : value.branches) {
        if (!remaining.has_value()) {
            break;
        }
        auto condition =
            (co_await complete_expression(branch.condition, std::move(remaining->state)));
        remaining = std::move(condition.normal);
        append_ownership_exits(result, condition);
        if (!remaining.has_value()) {
            break;
        }
        auto selected = (co_await region(branch.body, remaining->state));
        join_normal_ownership(result.normal, std::move(selected.normal));

        append_ownership_exits(result, selected);
    }
    if (remaining.has_value()) {
        if (value.otherwise.has_value()) {
            auto selected = (co_await region(**value.otherwise, std::move(remaining->state)));
            join_normal_ownership(result.normal, std::move(selected.normal));

            append_ownership_exits(result, selected);
        } else {
            join_normal_ownership(result.normal, std::move(remaining));
        }
    }
    co_return result;
}

auto OwnershipBodyAnalyzer::bind_pattern(
    OwnershipState& state,
    PatternID id,
    const OwnershipRelationships& relationships,
    std::span<const OwnershipPlace> places
) noexcept -> void {
    body.pattern(id).value.visit(
        Overloaded {
            [&](const BindingPattern& pattern) noexcept {
                if (std::holds_alternative<AliasBindingStorage>(
                        body.binding(pattern.binding).storage
                    )) {
                    auto& targets = selected_storage[pattern.binding];
                    targets.append_range(places);
                    std::ranges::sort(targets);
                    targets.erase(std::ranges::unique(targets).begin(), targets.end());
                    return;
                }
                store(

                    state,
                    binding_place(pattern.binding),
                    relationships,
                    body.pattern(id).origin
                );
            },
            [&](const EnumCasePattern& pattern) noexcept {
                for (const auto [index, child] : std::views::enumerate(pattern.payload)) {
                    auto projected = std::vector<OwnershipPlace>(places.begin(), places.end());
                    for (auto& place : projected) {
                        place.path.push_back(index);
                    }
                    bind_pattern(

                        state,
                        child,
                        project_relationships(relationships, OwnershipProjectionPath {index}),
                        projected
                    );
                }
            },
            [&](const OrPattern& pattern) noexcept {
                for (const auto alternative : pattern.alternatives) {
                    bind_pattern(state, alternative, relationships, places);
                }
            },
            [](const auto&) static noexcept {},
        }
    );
}

auto OwnershipBodyAnalyzer::pattern_condition(
    PatternID id,
    std::span<const SemPatternBounds> bounds,
    OwnershipState state
) noexcept -> ContinuationTask<OwnershipCondition> {
    const auto initial = [](OwnershipState input) static noexcept {
        return OwnershipCondition {
            .yes = OwnershipNormal {.state = std::move(input), .value = {}, .storage = {}},
            .no = {},
            .exits = {}
        };
    };
    const auto evaluate = [&](const SemanticExpression& bound, OwnershipState input) noexcept {
        return complete_expression(bound, std::move(input));
    };
    const auto join = [](std::optional<OwnershipNormal>& destination,
                         const std::optional<OwnershipNormal>& source) static noexcept {
        join_normal_ownership(destination, source);
    };
    co_return (co_await analyze_pattern_condition(
        program,
        body,
        id,
        bounds,
        std::move(state),
        initial,
        evaluate,
        join
    ));
}

auto OwnershipBodyAnalyzer::match(const SemMatch& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    auto subject = value.subject_is_place ? (co_await place(*value.subject, std::move(state)))
                                          : (co_await expression(*value.subject, std::move(state)));
    auto result = OwnershipFlow {};
    append_ownership_exits(result, subject);
    const auto subject_value = subject.normal ? subject.normal->value : OwnershipRelationships {};
    const auto previous_readers = storage_readers.size();
    protect_storage(subject_value);
    const auto subject_places =
        subject.normal ? subject.normal->storage : std::vector<OwnershipPlace>();
    auto remaining = std::move(subject.normal);
    for (const auto& arm : value.arms) {
        if (!arm.reachable || !remaining.has_value()) {
            continue;
        }

        struct RestoreSelections final {
            std::flat_map<LocalBindingID, std::vector<OwnershipPlace>>& current;
            std::flat_map<LocalBindingID, std::vector<OwnershipPlace>> saved;

            ~RestoreSelections() { current = std::move(saved); }
        };

        const auto selection_scope = RestoreSelections {selected_storage, selected_storage};
        auto selected = std::move(*remaining);
        bind_pattern(selected.state, arm.pattern, subject_value, subject_places);

        const auto previous_access = accesses.size();
        for (const auto& place : subject_places) {
            accesses.push_back({place, OwnershipAccessKind::Stable});
        }
        auto checked = (co_await pattern_condition(
            arm.pattern,
            arm.pattern_bounds,
            std::move(selected.state)
        ));
        accesses.resize(previous_access);
        if (!arm.pattern_may_reject) {
            checked.no.reset();
        }
        auto accepted = std::move(checked.yes);
        remaining = std::move(checked.no);
        result.exits.append_range(std::views::as_rvalue(checked.exits));
        if (accepted && arm.guard.has_value()) {
            const auto previous = accesses.size();
            for (const auto& place : subject_places) {
                accesses.push_back({place, OwnershipAccessKind::Stable});
            }
            auto guard = (co_await complete_expression(*arm.guard, std::move(accepted->state)));
            accesses.resize(previous);
            accepted = std::move(guard.normal);
            append_ownership_exits(result, guard);
            join_normal_ownership(remaining, accepted);
        }
        if (accepted.has_value()) {
            const auto previous = accesses.size();
            for (const auto binding : arm.bindings) {
                if (!std::holds_alternative<AliasBindingStorage>(body.binding(binding).storage)) {
                    continue;
                }
                for (const auto& selected : selected_storage.at(binding)) {
                    accesses.push_back({selected, OwnershipAccessKind::Structural});
                }
            }
            auto branch = (co_await region(arm.body, std::move(accepted->state)));
            accesses.resize(previous);
            join_normal_ownership(result.normal, std::move(branch.normal));
            append_ownership_exits(result, branch);
        }
        if (remaining.has_value()) {
            auto rejected = OwnershipFlow {.normal = std::move(remaining), .exits = {}};
            leave(rejected, arm.body.lifetime);
            remaining = std::move(rejected.normal);
        }
    }
    restore_storage_readers(previous_readers);
    co_return result;
}

auto OwnershipBodyAnalyzer::attempt(const SemTry& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    auto protected_flow = (co_await region(*value.body, std::move(state)));
    auto result = OwnershipFlow {
        .normal = std::move(protected_flow.normal),

        .exits = {}
    };
    auto pending = std::vector<OwnershipExit>();
    for (auto& exit : protected_flow.exits) {
        if (std::holds_alternative<OwnershipFailure>(exit.payload)) {
            pending.push_back(std::move(exit));
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    for (const auto& arm : value.arms) {
        auto rejected = std::vector<OwnershipExit>();
        for (auto& failure : pending) {
            const auto& accepted_types = facts.catches.at(std::addressof(arm));
            const auto found =
                accepted_types.find(std::get<OwnershipFailure>(failure.payload).type);
            if (found == accepted_types.end()) {
                rejected.push_back(std::move(failure));
                continue;
            }
            const auto& acceptance = found->second;
            auto remaining = std::optional<OwnershipNormal>();
            auto accepted = std::optional(OwnershipNormal {std::move(failure.state), {}, {}});
            for (const auto pattern : acceptance.alternatives) {
                if (pattern.has_value()) {
                    bind_pattern(
                        accepted->state,
                        *pattern,
                        std::get<OwnershipFailure>(failure.payload).value
                    );
                }
            }
            const auto previous_caught = caught;
            caught = std::get<OwnershipFailure>(failure.payload);
            const auto previous_readers = storage_readers.size();
            protect_storage(caught->value);
            remaining = std::move(accepted);
            accepted.reset();
            for (const auto pattern : acceptance.alternatives) {
                if (!remaining) {
                    break;
                }
                if (!pattern) {
                    join_normal_ownership(accepted, remaining);
                    remaining.reset();
                    break;
                }
                auto checked = (co_await pattern_condition(
                    *pattern,
                    arm.pattern_bounds,
                    std::move(remaining->state)
                ));
                join_normal_ownership(accepted, checked.yes);
                remaining = std::move(checked.no);
                result.exits.append_range(std::views::as_rvalue(checked.exits));
            }
            if (acceptance.exhaustive) {
                remaining.reset();
            }
            if (accepted && arm.guard.has_value()) {
                auto guard = (co_await complete_expression(*arm.guard, std::move(accepted->state)));
                accepted = std::move(guard.normal);
                append_ownership_exits(result, guard);
                join_normal_ownership(remaining, accepted);
            }
            if (accepted.has_value()) {
                auto handled = (co_await region(arm.body, std::move(accepted->state)));
                join_normal_ownership(result.normal, std::move(handled.normal));

                append_ownership_exits(result, handled);
            }
            caught = previous_caught;
            restore_storage_readers(previous_readers);
            if (remaining.has_value()) {
                auto next = OwnershipFlow {.normal = std::move(remaining), .exits = {}};
                leave(next, arm.body.lifetime);
                rejected.push_back(
                    {std::get<OwnershipFailure>(failure.payload), std::move(next.normal->state)}
                );
            }
        }
        pending = std::move(rejected);
    }
    const auto residual =
        program.failure_sets().failure_set(value.residual_failures.resolved()).members;
    for (auto& exit : pending) {
        if (std::ranges::contains(residual, std::get<OwnershipFailure>(exit.payload).type)) {
            result.exits.push_back(std::move(exit));
        }
    }
    co_return result;
}

auto OwnershipBodyAnalyzer::loop(const SemLoop& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    auto result = (co_await region(*value.initializer, std::move(state), false));
    if (!result.normal.has_value()) {
        co_return result;
    }
    const auto entry = std::move(result.normal->state);
    auto header = entry;
    const auto previous_feedback = feedback_origin;
    feedback_origin = value.body->origin;
    const auto iterate = [&](OwnershipState input) noexcept -> ContinuationTask<OwnershipFlow> {
        auto pass =
            OwnershipFlow {.normal = OwnershipNormal {std::move(input), {}, {}}, .exits = {}};
        if (value.condition.has_value()) {
            pass = (co_await complete_expression(*value.condition, std::move(pass.normal->state)));
        }
        auto iteration = OwnershipFlow {};
        append_ownership_exits(iteration, pass);
        if (!pass.normal.has_value()) {
            co_return iteration;
        }
        if (value.condition.has_value()) {
            iteration.exits.push_back({OwnershipBreak {}, pass.normal->state});
        }
        auto child = (co_await region(*value.body, std::move(pass.normal->state)));
        auto steps = std::move(child.normal);
        for (auto& exit : child.exits) {
            if (std::holds_alternative<OwnershipContinue>(exit.payload)) {
                join_normal_ownership(
                    steps,
                    std::optional(OwnershipNormal {std::move(exit.state), {}, {}})
                );
            } else {
                iteration.exits.push_back(std::move(exit));
            }
        }
        if (steps.has_value()) {
            auto step = (co_await region(*value.steps, std::move(steps->state), false));
            iteration.normal = std::move(step.normal);
            append_ownership_exits(iteration, step);
        }
        co_return iteration;
    };
    const auto previous = diagnosing;
    diagnosing = false;
    auto pass = OwnershipFlow {};
    auto reached_exits = OwnershipFlow {};
    for (;;) {
        const auto revision = topology.revision;
        pass = (co_await iterate(header));
        auto next = header;
        if (pass.normal.has_value()) {
            join_ownership_state(next, pass.normal->state);
        }
        synchronize_storage(next);
        synchronize_storage(header);
        const auto converged = next == header && revision == topology.revision;
        append_ownership_exits(reached_exits, pass);
        pass.exits.clear();
        // Semantic equality excludes diagnostic origins. Retain the witnesses
        // selected by this join even when no semantic fact changed.
        header = std::move(next);
        if (converged) {
            break;
        }
    }
    diagnosing = previous;
    if (diagnosing) {
        pass = (co_await iterate(std::move(header)));
    }
    append_ownership_exits(reached_exits, pass);
    pass.exits = std::move(reached_exits.exits);
    feedback_origin = previous_feedback;
    result.normal.reset();
    for (auto& exit : pass.exits) {
        if (std::holds_alternative<OwnershipBreak>(exit.payload)) {
            join_normal_ownership(
                result.normal,
                std::optional(OwnershipNormal {std::move(exit.state), {}, {}})
            );
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    leave(result, value.initializer->lifetime);
    co_return result;
}

auto OwnershipBodyAnalyzer::range(const SemRangeLoop& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    const auto& iterable = value.source;
    const auto range_value = std::holds_alternative<RangeTypeValue>(
        program.types().type(iterable.type.resolved()).value
    );
    auto result = iterable.category == SemanticValueCategory::Place
        ? (co_await place(iterable, std::move(state)))
        : (co_await expression(iterable, std::move(state)));
    if (!result.normal.has_value()) {
        co_return result;
    }
    const auto previous = accesses.size();
    const auto previous_readers = storage_readers.size();
    protect_storage(result.normal->value);
    const auto slice_value = std::holds_alternative<SliceTypeValue>(
        program.types().type(iterable.type.resolved()).value
    );
    auto elements = range_value ? std::vector<OwnershipPlace>()
                                : select_element_storage(
                                      iterable.type.resolved(),
                                      result.normal->storage,
                                      result.normal->value,
                                      std::nullopt,
                                      iterable.origin,
                                      result.normal->state
                                  );
    if (!range_value) {
        const auto sequence_value = std::holds_alternative<OwnedSequenceTypeValue>(
            program.types().type(iterable.type.resolved()).value
        );
        for (const auto& selected : sequence_value ? elements : result.normal->storage) {
            accesses.push_back(
                {selected,
                 sequence_value ? OwnershipAccessKind::Structural : selected_access_kind(iterable)}
            );
        }
    }
    const auto borrowed = value.binding.has_value()
        && value.access == AccessMode::Read
        && analysis.contents(body.binding(*value.binding).type).read_borrows_storage();
    if (borrowed) {
        selected_storage.emplace(*value.binding, elements);
    } else if (value.binding.has_value()
               && value.access == AccessMode::Write
               && !elements.empty()) {
        selected_storage.emplace(*value.binding, elements);
    }
    const auto entry = std::move(result.normal->state);
    auto header = entry;
    const auto previous_feedback = feedback_origin;
    feedback_origin = value.body->origin;
    auto initial_elements = OwnershipRelationships {};
    if (slice_value) {
        for (const auto& selected : elements) {
            merge_relationships(
                initial_elements,
                project_relationships(entry.objects[selected.object].relationships, selected.path)
            );
        }
    } else {
        initial_elements =
            project_relationships(result.normal->value, OwnershipProjectionPath {std::nullopt});
    }
    const auto iterate = [&](OwnershipState input) noexcept -> ContinuationTask<OwnershipFlow> {
        if (value.binding.has_value() && value.access != AccessMode::Write && !borrowed) {
            auto relationships = elements.empty() ? initial_elements : OwnershipRelationships {};
            for (const auto& element : elements) {
                merge_relationships(
                    relationships,
                    project_relationships(input.objects[element.object].relationships, element.path)
                );
            }
            store(
                input,
                binding_place(*value.binding),
                relationships,
                body.binding(*value.binding).origin
            );
        }
        co_return (co_await region(*value.body, std::move(input)));
    };
    const auto previous_diagnosing = diagnosing;
    diagnosing = false;
    auto pass = OwnershipFlow {};
    auto reached_exits = OwnershipFlow {};
    for (;;) {
        const auto revision = topology.revision;
        pass = (co_await iterate(header));
        auto next = header;
        if (pass.normal.has_value()) {
            join_ownership_state(next, pass.normal->state);
        }
        for (const auto& exit : pass.exits) {
            if (std::holds_alternative<OwnershipContinue>(exit.payload)) {
                join_ownership_state(next, exit.state);
            }
        }
        synchronize_storage(next);
        synchronize_storage(header);
        const auto converged = next == header && revision == topology.revision;
        append_ownership_exits(reached_exits, pass);
        pass.exits.clear();
        header = std::move(next);
        if (converged) {
            break;
        }
    }
    diagnosing = previous_diagnosing;
    if (diagnosing) {
        pass = (co_await iterate(header));
    }
    append_ownership_exits(reached_exits, pass);
    pass.exits = std::move(reached_exits.exits);
    feedback_origin = previous_feedback;
    result.normal = OwnershipNormal {std::move(header), {}, {}};
    for (auto& exit : pass.exits) {
        if (std::holds_alternative<OwnershipBreak>(exit.payload)) {
            join_normal_ownership(
                result.normal,
                std::optional(OwnershipNormal {std::move(exit.state), {}, {}})
            );
        } else if (!std::holds_alternative<OwnershipContinue>(exit.payload)) {
            result.exits.push_back(std::move(exit));
        }
    }
    if (value.binding.has_value()) {
        aliases.erase(*value.binding);
        selected_storage.erase(*value.binding);
    }
    accesses.resize(previous);
    restore_storage_readers(previous_readers);
    leave(result, value.lifetime);
    co_return result;
}
