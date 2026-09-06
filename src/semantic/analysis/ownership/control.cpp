module carven:semantic.analysis.ownership.control.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
import std;

namespace ownership {

auto BodyAnalyzer::conditional(const SemIf& value, State state) noexcept -> Flow {
    auto remaining = std::optional(std::move(state));
    auto result = Flow {};
    for (const auto& branch : value.branches) {
        if (!remaining.has_value()) {
            break;
        }
        auto condition = complete_expression(branch.condition, std::move(*remaining));
        remaining = std::move(condition.normal);
        append_exits(result, condition);
        if (!remaining.has_value()) {
            break;
        }
        const auto known = constant_truth(branch.condition);
        if (known != false) {
            auto selected = region(branch.body, *remaining);
            join_normal(result.normal, selected.normal);
            merge_relationships(result.value, selected.value);
            append_exits(result, selected);
        }
        if (known == true) {
            remaining.reset();
        }
    }
    if (remaining.has_value()) {
        if (value.otherwise.has_value()) {
            auto selected = region(**value.otherwise, std::move(*remaining));
            join_normal(result.normal, selected.normal);
            merge_relationships(result.value, selected.value);
            append_exits(result, selected);
        } else {
            join_normal(result.normal, remaining);
        }
    }
    return result;
}
auto BodyAnalyzer::bind_pattern(
    State& state,
    PatternID id,
    const Relationships& relationships
) noexcept -> void {
    std::visit(
        Overloaded {
            [&](const BindingPattern& pattern) noexcept {
                store(
                    state,
                    binding_place(pattern.binding),
                    relationships,
                    body.pattern(id).origin
                );
            },
            [&](const EnumCasePattern& pattern) noexcept {
                for (const auto [index, child] : std::views::enumerate(pattern.payload)) {
                    bind_pattern(state, child, project(relationships, ProjectionPath {index}));
                }
            },
            [&](const OrPattern& pattern) noexcept {
                for (const auto alternative : pattern.alternatives) {
                    bind_pattern(state, alternative, relationships);
                }
            },
            [](const auto&) static noexcept {},
        },
        body.pattern(id).value
    );
}
auto BodyAnalyzer::irrefutable(PatternID pattern) const noexcept -> bool {
    return facts.irrefutable_patterns.contains(pattern);
}
auto BodyAnalyzer::match(const SemMatch& value, State state) noexcept -> Flow {
    auto subject = value.subject_is_place ? place(*value.subject, std::move(state))
                                          : expression(*value.subject, std::move(state));
    auto result = Flow {};
    append_exits(result, subject);
    auto remaining = std::move(subject.normal);
    const auto subject_place = location(*value.subject);
    for (const auto& arm : value.arms) {
        if (!arm.reachable || !remaining.has_value()) {
            continue;
        }
        auto selected = *remaining;
        bind_pattern(selected, arm.pattern, subject.value);
        if (irrefutable(arm.pattern)) {
            remaining.reset();
        }
        auto accepted = std::optional(std::move(selected));
        if (arm.guard.has_value()) {
            const auto previous = accesses.size();
            if (subject_place.has_value()) {
                accesses.push_back({*subject_place, true});
            }
            auto guard = complete_expression(*arm.guard, std::move(*accepted));
            accesses.resize(previous);
            accepted = std::move(guard.normal);
            append_exits(result, guard);
            if (constant_truth(*arm.guard) != true) {
                join_normal(remaining, accepted);
            }
            if (constant_truth(*arm.guard) == false) {
                accepted.reset();
            }
        }
        if (accepted.has_value()) {
            auto branch = region(arm.body, std::move(*accepted));
            join_normal(result.normal, branch.normal);
            merge_relationships(result.value, branch.value);
            append_exits(result, branch);
        }
        if (remaining.has_value()) {
            auto rejected = Flow {.normal = std::move(remaining), .value = {}, .exits = {}};
            leave(rejected, arm.body.lifetime);
            remaining = std::move(rejected.normal);
        }
    }
    return result;
}
auto BodyAnalyzer::attempt(const SemTry& value, State state) noexcept -> Flow {
    auto protected_flow = region(*value.body, std::move(state));
    auto result = Flow {
        .normal = std::move(protected_flow.normal),
        .value = std::move(protected_flow.value),
        .exits = {}
    };
    auto pending = std::vector<Exit>();
    for (auto& exit : protected_flow.exits) {
        if (exit.kind == ExitKind::Failure) {
            pending.push_back(std::move(exit));
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    for (const auto& arm : value.arms) {
        auto rejected = std::vector<Exit>();
        for (auto& failure : pending) {
            const auto& accepted_types = facts.catches.at(std::addressof(arm));
            const auto found = accepted_types.find(*failure.failure);
            if (found == accepted_types.end()) {
                rejected.push_back(std::move(failure));
                continue;
            }
            const auto& acceptance = found->second;
            auto remaining =
                acceptance.exhaustive ? std::optional<State>() : std::optional(failure.state);
            auto accepted = std::optional(std::move(failure.state));
            for (const auto pattern : acceptance.alternatives) {
                if (pattern.has_value()) {
                    bind_pattern(*accepted, *pattern, {});
                }
            }
            const auto previous_caught = caught;
            caught = {*failure.failure};
            if (arm.guard.has_value()) {
                auto guard = complete_expression(*arm.guard, std::move(*accepted));
                accepted = std::move(guard.normal);
                append_exits(result, guard);
                if (constant_truth(*arm.guard) != true) {
                    join_normal(remaining, accepted);
                }
                if (constant_truth(*arm.guard) == false) {
                    accepted.reset();
                }
            }
            if (accepted.has_value()) {
                auto handled = region(arm.body, std::move(*accepted));
                join_normal(result.normal, handled.normal);
                merge_relationships(result.value, handled.value);
                append_exits(result, handled);
            }
            caught = previous_caught;
            if (remaining.has_value()) {
                auto next = Flow {.normal = std::move(remaining), .value = {}, .exits = {}};
                leave(next, arm.body.lifetime);
                rejected.push_back(
                    {ExitKind::Failure, failure.failure, std::move(*next.normal), {}}
                );
            }
        }
        pending = std::move(rejected);
    }
    const auto residual =
        draft.failure_sets().failure_set(value.residual_failures.resolved()).members;
    for (auto& exit : pending) {
        if (std::ranges::contains(residual, *exit.failure)) {
            result.exits.push_back(std::move(exit));
        }
    }
    return result;
}
auto BodyAnalyzer::loop(const SemLoop& value, State state) noexcept -> Flow {
    auto result = region(*value.initializer, std::move(state), false);
    if (!result.normal.has_value()) {
        return result;
    }
    const auto entry = *result.normal;
    auto header = entry;
    const auto iterate = [&](State input) noexcept -> Flow {
        auto pass = Flow {.normal = std::move(input), .value = {}, .exits = {}};
        if (value.condition.has_value()) {
            pass = complete_expression(*value.condition, std::move(*pass.normal));
        }
        auto iteration = Flow {};
        append_exits(iteration, pass);
        if (!pass.normal.has_value()) {
            return iteration;
        }
        const auto known =
            value.condition.has_value() ? constant_truth(*value.condition) : std::optional(true);
        if (known != true) {
            iteration.exits.push_back({ExitKind::Break, std::nullopt, *pass.normal, {}});
        }
        if (known == false) {
            return iteration;
        }
        auto child = region(*value.body, std::move(*pass.normal));
        auto steps = std::move(child.normal);
        for (auto& exit : child.exits) {
            if (exit.kind == ExitKind::Continue) {
                join_normal(steps, std::optional(exit.state));
            } else {
                iteration.exits.push_back(std::move(exit));
            }
        }
        if (steps.has_value()) {
            auto step = region(*value.steps, std::move(*steps), false);
            iteration.normal = std::move(step.normal);
            append_exits(iteration, step);
        }
        return iteration;
    };
    const auto previous = diagnosing;
    diagnosing = false;
    for (;;) {
        const auto pass = iterate(header);
        auto next = entry;
        if (pass.normal.has_value()) {
            join(next, *pass.normal);
        }
        if (next == header) {
            break;
        }
        header = std::move(next);
    }
    diagnosing = previous;
    auto pass = iterate(std::move(header));
    result.normal.reset();
    for (auto& exit : pass.exits) {
        if (exit.kind == ExitKind::Break) {
            join_normal(result.normal, std::optional(exit.state));
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    leave(result, value.initializer->lifetime);
    return result;
}
auto BodyAnalyzer::range(const SemRangeLoop& value, State state) noexcept -> Flow {
    const auto source = location(value.begin);
    auto result = !value.end.has_value() && source.has_value()
        ? place(value.begin, std::move(state))
        : expression(value.begin, std::move(state));
    if (result.normal.has_value() && value.end.has_value()) {
        auto end = expression(*value.end, std::move(*result.normal));
        result.normal = std::move(end.normal);
        append_exits(result, end);
    }
    if (!result.normal.has_value()) {
        return result;
    }
    const auto previous = accesses.size();
    auto elements = std::optional<Place>();
    if (!value.end.has_value() && source.has_value()) {
        accesses.push_back({*source, false});
        elements = *source;
        elements->path.push_back(std::nullopt);
    }
    if (value.binding.has_value() && value.access == AccessMode::Write && elements.has_value()) {
        aliases.emplace(*value.binding, *elements);
    }
    const auto entry = *result.normal;
    auto header = entry;
    const auto initial_elements = project(result.value, ProjectionPath {std::nullopt});
    const auto iterate = [&](State input) noexcept -> Flow {
        if (value.binding.has_value() && value.access != AccessMode::Write) {
            const auto relationships = elements.has_value()
                ? project(input.objects[elements->object].relationships, elements->path)
                : initial_elements;
            store(
                input,
                binding_place(*value.binding),
                relationships,
                body.binding(*value.binding).origin
            );
        }
        return region(*value.body, std::move(input));
    };
    const auto previous_diagnosing = diagnosing;
    diagnosing = false;
    for (;;) {
        const auto pass = iterate(header);
        auto next = entry;
        if (pass.normal.has_value()) {
            join(next, *pass.normal);
        }
        for (const auto& exit : pass.exits) {
            if (exit.kind == ExitKind::Continue) {
                join(next, exit.state);
            }
        }
        if (next == header) {
            break;
        }
        header = std::move(next);
    }
    diagnosing = previous_diagnosing;
    auto pass = iterate(header);
    result.normal = std::move(header);
    for (auto& exit : pass.exits) {
        if (exit.kind == ExitKind::Break) {
            join_normal(result.normal, std::optional(exit.state));
        } else if (exit.kind != ExitKind::Continue) {
            result.exits.push_back(std::move(exit));
        }
    }
    if (value.binding.has_value()) {
        aliases.erase(*value.binding);
    }
    accesses.resize(previous);
    leave(result, value.lifetime);
    return result;
}

} // namespace ownership
