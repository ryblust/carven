module carven:semantic.analysis.ownership.control.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
import std;

auto OwnershipBodyAnalyzer::conditional(const SemIf& value, OwnershipState state) noexcept
    -> OwnershipFlow {
    auto remaining = std::optional(OwnershipNormal {std::move(state), {}, {}});
    auto result = OwnershipFlow {};
    for (const auto& branch : value.branches) {
        if (!remaining.has_value()) {
            break;
        }
        auto condition = complete_expression(branch.condition, std::move(remaining->state));
        remaining = std::move(condition.normal);
        append_ownership_exits(result, condition);
        if (!remaining.has_value()) {
            break;
        }
        const auto known = constant_truth(branch.condition);
        if (known != false) {
            auto selected = region(branch.body, remaining->state);
            join_normal_ownership(result.normal, selected.normal);

            append_ownership_exits(result, selected);
        }
        if (known == true) {
            remaining.reset();
        }
    }
    if (remaining.has_value()) {
        if (value.otherwise.has_value()) {
            auto selected = region(**value.otherwise, std::move(remaining->state));
            join_normal_ownership(result.normal, selected.normal);

            append_ownership_exits(result, selected);
        } else {
            join_normal_ownership(result.normal, remaining);
        }
    }
    return result;
}

auto OwnershipBodyAnalyzer::bind_pattern(
    OwnershipState& state,
    PatternID id,
    const OwnershipRelationships& relationships
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
                    bind_pattern(
                        state,
                        child,
                        project_relationships(relationships, OwnershipProjectionPath {index})
                    );
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

auto OwnershipBodyAnalyzer::irrefutable(PatternID pattern) const noexcept -> bool {
    return facts.irrefutable_patterns.contains(pattern);
}

auto OwnershipBodyAnalyzer::match(const SemMatch& value, OwnershipState state) noexcept
    -> OwnershipFlow {
    auto subject = value.subject_is_place ? place(*value.subject, std::move(state))
                                          : expression(*value.subject, std::move(state));
    auto result = OwnershipFlow {};
    append_ownership_exits(result, subject);
    const auto subject_value = subject.normal ? subject.normal->value : OwnershipRelationships {};
    const auto previous_readers = storage_readers.size();
    protect_storage(subject_value);
    auto remaining = std::move(subject.normal);
    const auto subject_place = location(*value.subject);
    for (const auto& arm : value.arms) {
        if (!arm.reachable || !remaining.has_value()) {
            continue;
        }
        auto selected = *remaining;
        bind_pattern(selected.state, arm.pattern, subject_value);
        if (irrefutable(arm.pattern)) {
            remaining.reset();
        }
        auto accepted = std::optional(std::move(selected));
        if (arm.guard.has_value()) {
            const auto previous = accesses.size();
            if (subject_place.has_value()) {
                accesses.push_back({*subject_place, true});
            }
            auto guard = complete_expression(*arm.guard, std::move(accepted->state));
            accesses.resize(previous);
            accepted = std::move(guard.normal);
            append_ownership_exits(result, guard);
            if (constant_truth(*arm.guard) != true) {
                join_normal_ownership(remaining, accepted);
            }
            if (constant_truth(*arm.guard) == false) {
                accepted.reset();
            }
        }
        if (accepted.has_value()) {
            auto branch = region(arm.body, std::move(accepted->state));
            join_normal_ownership(result.normal, branch.normal);

            append_ownership_exits(result, branch);
        }
        if (remaining.has_value()) {
            auto rejected = OwnershipFlow {.normal = std::move(remaining), .exits = {}};
            leave(rejected, arm.body.lifetime);
            remaining = std::move(rejected.normal);
        }
    }
    restore_storage_readers(previous_readers);
    return result;
}

auto OwnershipBodyAnalyzer::attempt(const SemTry& value, OwnershipState state) noexcept
    -> OwnershipFlow {
    auto protected_flow = region(*value.body, std::move(state));
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
            auto remaining = acceptance.exhaustive
                ? std::optional<OwnershipNormal>()
                : std::optional(OwnershipNormal {failure.state, {}, {}});
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
            if (arm.guard.has_value()) {
                auto guard = complete_expression(*arm.guard, std::move(accepted->state));
                accepted = std::move(guard.normal);
                append_ownership_exits(result, guard);
                if (constant_truth(*arm.guard) != true) {
                    join_normal_ownership(remaining, accepted);
                }
                if (constant_truth(*arm.guard) == false) {
                    accepted.reset();
                }
            }
            if (accepted.has_value()) {
                auto handled = region(arm.body, std::move(accepted->state));
                join_normal_ownership(result.normal, handled.normal);

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
    return result;
}

auto OwnershipBodyAnalyzer::loop(const SemLoop& value, OwnershipState state) noexcept
    -> OwnershipFlow {
    auto result = region(*value.initializer, std::move(state), false);
    if (!result.normal.has_value()) {
        return result;
    }
    const auto entry = result.normal->state;
    auto header = entry;
    const auto iterate = [&](OwnershipState input) noexcept -> OwnershipFlow {
        auto pass =
            OwnershipFlow {.normal = OwnershipNormal {std::move(input), {}, {}}, .exits = {}};
        if (value.condition.has_value()) {
            pass = complete_expression(*value.condition, std::move(pass.normal->state));
        }
        auto iteration = OwnershipFlow {};
        append_ownership_exits(iteration, pass);
        if (!pass.normal.has_value()) {
            return iteration;
        }
        const auto known =
            value.condition.has_value() ? constant_truth(*value.condition) : std::optional(true);
        if (known != true) {
            iteration.exits.push_back({OwnershipBreak {}, pass.normal->state});
        }
        if (known == false) {
            return iteration;
        }
        auto child = region(*value.body, std::move(pass.normal->state));
        auto steps = std::move(child.normal);
        for (auto& exit : child.exits) {
            if (std::holds_alternative<OwnershipContinue>(exit.payload)) {
                join_normal_ownership(steps, std::optional(OwnershipNormal {exit.state, {}, {}}));
            } else {
                iteration.exits.push_back(std::move(exit));
            }
        }
        if (steps.has_value()) {
            auto step = region(*value.steps, std::move(steps->state), false);
            iteration.normal = std::move(step.normal);
            append_ownership_exits(iteration, step);
        }
        return iteration;
    };
    const auto previous = diagnosing;
    diagnosing = false;
    auto pass = OwnershipFlow {};
    for (;;) {
        pass = iterate(header);
        auto next = entry;
        if (pass.normal.has_value()) {
            join_ownership_state(next, pass.normal->state);
        }
        if (next == header) {
            break;
        }
        header = std::move(next);
    }
    diagnosing = previous;
    if (diagnosing) {
        pass = iterate(std::move(header));
    }
    result.normal.reset();
    for (auto& exit : pass.exits) {
        if (std::holds_alternative<OwnershipBreak>(exit.payload)) {
            join_normal_ownership(
                result.normal,
                std::optional(OwnershipNormal {exit.state, {}, {}})
            );
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    leave(result, value.initializer->lifetime);
    return result;
}

auto OwnershipBodyAnalyzer::range(const SemRangeLoop& value, OwnershipState state) noexcept
    -> OwnershipFlow {
    const auto* integer = std::get_if<SemIntegerRange>(&value.source);
    const auto& first = integer ? integer->begin : std::get<SemSequenceRange>(value.source).value;

    const auto source = location(first);
    auto result = integer == nullptr && source.has_value() ? place(first, std::move(state))
                                                           : expression(first, std::move(state));
    if (result.normal.has_value() && integer != nullptr) {
        auto end = expression(integer->end, std::move(result.normal->state));
        result.normal = std::move(end.normal);
        append_ownership_exits(result, end);
    }
    if (!result.normal.has_value()) {
        return result;
    }
    const auto previous = accesses.size();
    const auto previous_readers = storage_readers.size();
    protect_storage(result.normal->value);
    auto elements = std::optional<OwnershipPlace>();
    if (integer == nullptr) {
        for (const auto& selected : result.normal->storage) {
            accesses.push_back({selected, false});
        }
    }
    if (integer == nullptr && source.has_value()) {
        elements = *source;
        elements->path.push_back(std::nullopt);
    }
    const auto borrowed = value.binding.has_value()
        && value.access == AccessMode::Read
        && analysis.contents(body.binding(*value.binding).type).read_borrows_storage();
    if (borrowed) {
        read_storage.emplace(
            *value.binding,
            select_element_storage(
                program.types(),
                first.type.resolved(),
                result.normal->storage,
                result.normal->value,
                std::nullopt
            )
        );
    } else if (value.binding.has_value()
               && value.access == AccessMode::Write
               && elements.has_value()) {
        aliases.emplace(*value.binding, *elements);
    }
    const auto entry = result.normal->state;
    auto header = entry;
    const auto initial_elements =
        project_relationships(result.normal->value, OwnershipProjectionPath {std::nullopt});
    const auto iterate = [&](OwnershipState input) noexcept -> OwnershipFlow {
        if (value.binding.has_value() && value.access != AccessMode::Write && !borrowed) {
            const auto relationships = elements.has_value()
                ? project_relationships(
                      input.objects[elements->object].relationships,
                      elements->path
                  )
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
    auto pass = OwnershipFlow {};
    for (;;) {
        pass = iterate(header);
        auto next = entry;
        if (pass.normal.has_value()) {
            join_ownership_state(next, pass.normal->state);
        }
        for (const auto& exit : pass.exits) {
            if (std::holds_alternative<OwnershipContinue>(exit.payload)) {
                join_ownership_state(next, exit.state);
            }
        }
        if (next == header) {
            break;
        }
        header = std::move(next);
    }
    diagnosing = previous_diagnosing;
    if (diagnosing) {
        pass = iterate(header);
    }
    result.normal = OwnershipNormal {std::move(header), {}, {}};
    for (auto& exit : pass.exits) {
        if (std::holds_alternative<OwnershipBreak>(exit.payload)) {
            join_normal_ownership(
                result.normal,
                std::optional(OwnershipNormal {exit.state, {}, {}})
            );
        } else if (!std::holds_alternative<OwnershipContinue>(exit.payload)) {
            result.exits.push_back(std::move(exit));
        }
    }
    if (value.binding.has_value()) {
        aliases.erase(*value.binding);
        read_storage.erase(*value.binding);
    }
    accesses.resize(previous);
    restore_storage_readers(previous_readers);
    leave(result, value.lifetime);
    return result;
}
