module carven:semantic.analysis.ownership.control.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
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
        const auto known = constant_truth(branch.condition);
        if (known != false) {
            auto selected = (co_await region(branch.body, remaining->state));
            join_normal_ownership(result.normal, selected.normal);

            append_ownership_exits(result, selected);
        }
        if (known == true) {
            remaining.reset();
        }
    }
    if (remaining.has_value()) {
        if (value.otherwise.has_value()) {
            auto selected = (co_await region(**value.otherwise, std::move(remaining->state)));
            join_normal_ownership(result.normal, selected.normal);

            append_ownership_exits(result, selected);
        } else {
            join_normal_ownership(result.normal, remaining);
        }
    }
    co_return result;
}

auto OwnershipBodyAnalyzer::bind_pattern(
    OwnershipState& state,
    PatternID id,
    const OwnershipRelationships& relationships
) noexcept -> void {
    body.pattern(id).value.visit(
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
        }
    );
}

auto OwnershipBodyAnalyzer::irrefutable(PatternID pattern) const noexcept -> bool {
    return facts.irrefutable_patterns.contains(pattern);
}

auto OwnershipBodyAnalyzer::pattern_condition(
    PatternID id,
    std::span<const SemPatternBounds> bounds,
    OwnershipState state
) noexcept -> ContinuationTask<OwnershipCondition> {
    auto result = OwnershipCondition {
        .yes = OwnershipNormal {.state = std::move(state), .value = {}, .storage = {}},
        .no = {},
        .exits = {}
    };
    const auto& pattern = body.pattern(id).value;
    const auto sequence = [&](PatternID child) noexcept -> ContinuationTask<std::monostate> {
        if (!result.yes) {
            co_return {};
        }
        auto next = (co_await pattern_condition(child, bounds, std::move(result.yes->state)));
        result.yes = std::move(next.yes);
        join_normal_ownership(result.no, next.no);
        result.exits.append_range(std::views::as_rvalue(next.exits));
        co_return {};
    };
    if (const auto* alternatives = std::get_if<OrPattern>(&pattern)) {
        result.no = std::move(result.yes);
        result.yes.reset();
        for (const auto child : alternatives->alternatives) {
            if (!result.no) {
                break;
            }
            auto next = (co_await pattern_condition(child, bounds, std::move(result.no->state)));
            join_normal_ownership(result.yes, next.yes);
            result.no = std::move(next.no);
            result.exits.append_range(std::views::as_rvalue(next.exits));
        }
    } else if (const auto* enumeration = std::get_if<EnumCasePattern>(&pattern)) {
        const auto owner = program.declarations().enum_case(enumeration->enum_case).owner;
        if (program.declarations().enumeration(owner).cases.size() > 1uz) {
            result.no = result.yes;
        }
        for (const auto child : enumeration->payload) {
            (co_await sequence(child));
        }
    } else {
        const auto found = std::ranges::find(bounds, id, &SemPatternBounds::pattern);
        if (found != bounds.end()) {
            for (const auto* bound : {&found->begin, &found->end}) {
                if (!*bound || !result.yes) {
                    continue;
                }
                auto next = (co_await complete_expression(**bound, std::move(result.yes->state)));
                result.yes = std::move(next.normal);
                result.exits.append_range(std::views::as_rvalue(next.exits));
            }
        }
        result.no = result.yes;
    }
    if (irrefutable(id)) {
        result.no.reset();
    }
    co_return result;
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
    auto remaining = std::move(subject.normal);
    const auto subject_place = location(*value.subject);
    for (const auto& arm : value.arms) {
        if (!arm.reachable || !remaining.has_value()) {
            continue;
        }
        auto selected = *remaining;
        bind_pattern(selected.state, arm.pattern, subject_value);
        const auto previous_access = accesses.size();
        if (subject_place) {
            accesses.push_back({*subject_place, true});
        }
        auto checked = (co_await pattern_condition(
            arm.pattern,
            arm.pattern_bounds,
            std::move(selected.state)
        ));
        accesses.resize(previous_access);
        auto accepted = std::move(checked.yes);
        remaining = std::move(checked.no);
        result.exits.append_range(std::views::as_rvalue(checked.exits));
        if (accepted && arm.guard.has_value()) {
            const auto previous = accesses.size();
            if (subject_place.has_value()) {
                accesses.push_back({*subject_place, true});
            }
            auto guard = (co_await complete_expression(*arm.guard, std::move(accepted->state)));
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
            auto branch = (co_await region(arm.body, std::move(accepted->state)));
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
                if (constant_truth(*arm.guard) != true) {
                    join_normal_ownership(remaining, accepted);
                }
                if (constant_truth(*arm.guard) == false) {
                    accepted.reset();
                }
            }
            if (accepted.has_value()) {
                auto handled = (co_await region(arm.body, std::move(accepted->state)));
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
    co_return result;
}

auto OwnershipBodyAnalyzer::loop(const SemLoop& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    auto result = (co_await region(*value.initializer, std::move(state), false));
    if (!result.normal.has_value()) {
        co_return result;
    }
    const auto entry = result.normal->state;
    auto header = entry;
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
        const auto known =
            value.condition.has_value() ? constant_truth(*value.condition) : std::optional(true);
        if (known != true) {
            iteration.exits.push_back({OwnershipBreak {}, pass.normal->state});
        }
        if (known == false) {
            co_return iteration;
        }
        auto child = (co_await region(*value.body, std::move(pass.normal->state)));
        auto steps = std::move(child.normal);
        for (auto& exit : child.exits) {
            if (std::holds_alternative<OwnershipContinue>(exit.payload)) {
                join_normal_ownership(steps, std::optional(OwnershipNormal {exit.state, {}, {}}));
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
    for (;;) {
        pass = (co_await iterate(header));
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
        pass = (co_await iterate(std::move(header)));
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
    co_return result;
}

auto OwnershipBodyAnalyzer::range(const SemRangeLoop& value, OwnershipState state) noexcept
    -> ContinuationTask<OwnershipFlow> {
    const auto& iterable = value.source;
    const auto range_value = std::holds_alternative<RangeTypeValue>(
        program.types().type(iterable.type.resolved()).value
    );
    const auto source = range_value ? std::nullopt : location(iterable);
    auto result = source ? (co_await place(iterable, std::move(state)))
                         : (co_await expression(iterable, std::move(state)));
    if (!result.normal.has_value()) {
        co_return result;
    }
    const auto previous = accesses.size();
    const auto previous_readers = storage_readers.size();
    protect_storage(result.normal->value);
    auto elements = std::optional<OwnershipPlace>();
    if (!range_value) {
        for (const auto& selected : result.normal->storage) {
            accesses.push_back({selected, false});
        }
    }
    if (source.has_value()) {
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
                iterable.type.resolved(),
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
    const auto iterate = [&](OwnershipState input) noexcept -> ContinuationTask<OwnershipFlow> {
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
        co_return (co_await region(*value.body, std::move(input)));
    };
    const auto previous_diagnosing = diagnosing;
    diagnosing = false;
    auto pass = OwnershipFlow {};
    for (;;) {
        pass = (co_await iterate(header));
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
        pass = (co_await iterate(header));
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
    co_return result;
}
