module carven:semantic.analysis.nullability.control.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.nullability.context;
import :semantic.analysis.pattern.control;
import :support.invariant;
import :support.visit;
import std;

auto NullabilityBodyAnalyzer::region(const SemanticRegion& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto flow =
        NullFlow {.normal = NullNormal {.state = std::move(state), .value = {}}, .exits = {}};
    for (const auto& item : source.statements) {
        if (!flow.normal) {
            break;
        }
        auto next = (co_await statement(item, std::move(flow.normal->state)));
        flow.normal = std::move(next.normal);
        append_null_exits(flow.exits, std::move(next.exits));
    }
    if (flow.normal && source.result) {
        auto next = (co_await expression(*source.result, std::move(flow.normal->state)));
        flow.normal = std::move(next.normal);
        append_null_exits(flow.exits, std::move(next.exits));
    }
    co_return flow;
}

auto NullabilityBodyAnalyzer::statement(const SemanticStatement& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto flow =
        NullFlow {.normal = NullNormal {.state = std::move(state), .value = {}}, .exits = {}};
    const auto evaluate =
        [&](const SemanticExpression& value) noexcept -> ContinuationTask<std::monostate> {
        if (!flow.normal) {
            co_return {};
        }
        auto next = (co_await expression(value, std::move(flow.normal->state)));
        flow.normal = std::move(next.normal);
        append_null_exits(flow.exits, std::move(next.exits));
        co_return {};
    };
    const auto transfer = [&](NullExitPayload payload) noexcept {
        if (flow.normal) {
            flow.exits.push_back({.payload = payload, .state = std::move(flow.normal->state)});
            flow.normal.reset();
        }
    };
    (co_await source.value.visit(
        Overloaded {
            [&](const SemReturn& value) noexcept -> ContinuationTask<std::monostate> {
                if (value.value) {
                    (co_await evaluate(*value.value));
                }
                transfer(NullTransfer::Return);
                co_return {};
            },
            [&](const SemBreak&) noexcept -> ContinuationTask<std::monostate> {
                transfer(NullTransfer::Break);
                co_return {};
            },
            [&](const SemContinue&) noexcept -> ContinuationTask<std::monostate> {
                transfer(NullTransfer::Continue);
                co_return {};
            },
            [&](const SemRethrow&) noexcept -> ContinuationTask<std::monostate> {
                if (flow.normal) {
                    for (const auto type : caught) {
                        flow.exits.push_back({.payload = type, .state = flow.normal->state});
                    }
                    flow.normal.reset();
                }
                co_return {};
            },
            [&](const SemThrow& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.value));
                transfer(value.failure_type);
                co_return {};
            },
            [&](const SemExpressionStatement& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.expression));
                co_return {};
            },
            [&](const SemStaticBinding& value) noexcept -> ContinuationTask<std::monostate> {
                if (value.initializer->constant) {
                    (co_await evaluate(*value.initializer));
                    if (flow.normal) {
                        store(
                            flow.normal->state,
                            {.root = value.binding, .path = {}},
                            flow.normal->value
                        );
                    }
                }
                co_return {};
            },
            [&](const SemInitialize& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.initializer));
                if (flow.normal) {
                    const auto facts = flow.normal->value;
                    store(flow.normal->state, {.root = value.binding, .path = {}}, facts);
                }
                co_return {};
            },
            [&](const SemAsyncLet& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.initializer));
                if (flow.normal) {
                    store(flow.normal->state, {.root = value.child, .path = {}}, {});
                }
                co_return {};
            },
            [&](const SemAssign& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await evaluate(value.target));
                (co_await evaluate(value.value));
                if (!flow.normal) {
                    co_return {};
                }
                const auto facts = flow.normal->value;
                if (const auto target = location(value.target)) {
                    store(flow.normal->state, *target, facts);
                } else {
                    invalidate(flow.normal->state, location(value.target, true));
                }
                co_return {};
            },
            [&](const SemLoop& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await loop(value, std::move(flow.normal->state)));
                co_return {};
            },
            [&](const SemRangeLoop& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await range(value, std::move(flow.normal->state)));
                co_return {};
            },
            [](const SemExpandedLoop&) static noexcept -> ContinuationTask<std::monostate> {
                invariant_violation("expanded loop reached generic body analysis");
                co_return {};
            },
            [&](const SemConstBlock& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await region(*value.region, std::move(flow.normal->state)));
                co_return {};
            },
            [&](const OwnedSemanticRegion& value) noexcept -> ContinuationTask<std::monostate> {
                flow = (co_await region(*value, std::move(flow.normal->state)));
                co_return {};
            },
        }
    ));
    if (flow.normal) {
        flow.normal->value = {};
    }
    co_return flow;
}

auto NullabilityBodyAnalyzer::conditional(const SemIf& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto remaining = std::optional(NullNormal {.state = std::move(state), .value = {}});
    auto result = NullFlow();
    for (const auto& branch : source.branches) {
        if (!remaining) {
            break;
        }
        auto checked = (co_await condition(branch.condition, std::move(remaining->state)));
        remaining = std::move(checked.no);
        append_null_exits(result.exits, std::move(checked.exits));
        if (checked.yes) {
            auto selected = (co_await region(branch.body, std::move(checked.yes->state)));
            join_null_normal(result.normal, selected.normal);
            append_null_exits(result.exits, std::move(selected.exits));
        }
    }
    if (remaining && source.otherwise) {
        auto selected = (co_await region(**source.otherwise, std::move(remaining->state)));
        join_null_normal(result.normal, selected.normal);
        append_null_exits(result.exits, std::move(selected.exits));
    } else {
        join_null_normal(result.normal, remaining);
    }
    co_return result;
}

auto NullabilityBodyAnalyzer::bind_pattern(
    NullState& state,
    PatternID id,
    const NullValue& value
) const noexcept -> void {
    body.pattern(id).value.visit(
        Overloaded {
            [&](const BindingPattern& pattern) noexcept {
                store(state, {.root = pattern.binding, .path = {}}, value);
            },
            [&](const EnumCasePattern& pattern) noexcept {
                for (const auto [index, child] : std::views::enumerate(pattern.payload)) {
                    bind_pattern(state, child, project_null_value(value, index));
                }
            },
            [&](const OrPattern& pattern) noexcept {
                // Alternative layouts can differ; every bound ptr starts unknown.
                for (const auto child : pattern.alternatives) {
                    bind_pattern(state, child, {});
                }
            },
            [](const auto&) static noexcept {},
        }
    );
}

auto NullabilityBodyAnalyzer::pattern_condition(
    PatternID id,
    std::span<const SemPatternBounds> bounds,
    NullState state
) noexcept -> ContinuationTask<NullCondition> {
    const auto initial = [](NullState input) static noexcept {
        return NullCondition {
            .yes = NullNormal {.state = std::move(input), .value = {}},
            .no = {},
            .exits = {}
        };
    };
    const auto evaluate = [&](const SemanticExpression& bound, NullState input) noexcept {
        return expression(bound, std::move(input));
    };
    co_return (co_await analyze_pattern_condition(
        program,
        body,
        id,
        bounds,
        std::move(state),
        initial,
        evaluate,
        join_null_normal
    ));
}

auto NullabilityBodyAnalyzer::match(const SemMatch& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto subject = (co_await expression(*source.subject, std::move(state)));
    auto result = NullFlow {.normal = {}, .exits = std::move(subject.exits)};
    auto remaining = std::move(subject.normal);
    const auto saved = remaining ? remaining->value : NullValue();
    for (const auto& arm : source.arms) {
        if (!remaining || !arm.reachable) {
            continue;
        }
        auto selected = *remaining;
        const auto place = source.subject_is_place ? location(*source.subject) : std::nullopt;
        bind_pattern(selected.state, arm.pattern, place ? value_at(selected.state, *place) : saved);
        auto checked_pattern = (co_await pattern_condition(
            arm.pattern,
            arm.pattern_bounds,
            std::move(selected.state)
        ));
        auto accepted = std::move(checked_pattern.yes);
        if (!arm.pattern_may_reject) {
            checked_pattern.no.reset();
        }
        remaining = std::move(checked_pattern.no);
        append_null_exits(result.exits, std::move(checked_pattern.exits));
        if (accepted && arm.guard) {
            auto checked = (co_await condition(*arm.guard, std::move(accepted->state)));
            accepted = std::move(checked.yes);
            join_null_normal(remaining, checked.no);
            append_null_exits(result.exits, std::move(checked.exits));
        }
        if (accepted) {
            auto branch = (co_await region(arm.body, std::move(accepted->state)));
            join_null_normal(result.normal, branch.normal);
            append_null_exits(result.exits, std::move(branch.exits));
        }
    }
    co_return result;
}

auto NullabilityBodyAnalyzer::attempt(const SemTry& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto protected_flow = (co_await region(*source.body, std::move(state)));
    auto result = NullFlow {.normal = std::move(protected_flow.normal), .exits = {}};
    auto pending = std::vector<NullExit>();
    for (auto& exit : protected_flow.exits) {
        if (std::holds_alternative<TypeID>(exit.payload)) {
            pending.push_back(std::move(exit));
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    for (const auto& arm : source.arms) {
        auto rejected = std::vector<NullExit>();
        for (auto& failure : pending) {
            const auto* type = std::get_if<TypeID>(&failure.payload);
            if (type == nullptr) {
                invariant_violation("catch processing received a non-failure exit");
            }
            if (!std::ranges::contains(
                    program.failure_sets().failure_set(arm.accepted_failures.resolved()).members,
                    *type
                )) {
                rejected.push_back(std::move(failure));
                continue;
            }
            auto accepted = std::optional(NullNormal {.state = failure.state, .value = {}});
            auto alternatives = std::vector<std::optional<PatternID>>();
            for (const auto& alternative : arm.alternatives) {
                if (!alternative.reachable) {
                    continue;
                }
                if (std::holds_alternative<CatchAllPattern>(alternative.pattern)) {
                    alternatives.push_back(std::nullopt);
                } else if (const auto* typed =
                               std::get_if<SemTypedCatchPattern>(&alternative.pattern);
                           typed->type.resolved() == *type) {
                    alternatives.push_back(typed->inner);
                    bind_pattern(accepted->state, typed->inner, {});
                }
            }
            const auto arms = std::array {
                PatternCoverageArm {.alternatives = std::move(alternatives), .guarded = false},
            };
            const auto exhaustive = patterns_exhaustive(program, body.pattern_table(), *type, arms);
            if (!exhaustive) {
                invariant_violation(exhaustive.error());
            }
            auto remaining = std::optional<NullNormal>();
            const auto previous = caught;
            caught = {*type};
            remaining = std::move(accepted);
            accepted.reset();
            for (const auto& alternative : arm.alternatives) {
                if (!alternative.reachable || !remaining) {
                    continue;
                }
                if (std::holds_alternative<CatchAllPattern>(alternative.pattern)) {
                    join_null_normal(accepted, remaining);
                    remaining.reset();
                    break;
                }
                const auto& typed = std::get<SemTypedCatchPattern>(alternative.pattern);
                if (typed.type.resolved() != *type) {
                    continue;
                }
                auto checked = (co_await pattern_condition(
                    typed.inner,
                    arm.pattern_bounds,
                    std::move(remaining->state)
                ));
                join_null_normal(accepted, checked.yes);
                remaining = std::move(checked.no);
                append_null_exits(result.exits, std::move(checked.exits));
            }
            if (*exhaustive) {
                remaining.reset();
            }
            if (accepted && arm.guard) {
                auto checked = (co_await condition(*arm.guard, std::move(accepted->state)));
                accepted = std::move(checked.yes);
                join_null_normal(remaining, checked.no);
                append_null_exits(result.exits, std::move(checked.exits));
            }
            if (accepted) {
                auto handled = (co_await region(arm.body, std::move(accepted->state)));
                join_null_normal(result.normal, handled.normal);
                append_null_exits(result.exits, std::move(handled.exits));
            }
            caught = previous;
            if (remaining) {
                rejected.push_back({.payload = *type, .state = std::move(remaining->state)});
            }
        }
        pending = std::move(rejected);
    }
    append_null_exits(result.exits, std::move(pending));
    co_return result;
}

auto NullabilityBodyAnalyzer::loop(const SemLoop& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto initial = (co_await region(*source.initializer, std::move(state)));
    auto result = NullFlow {.normal = {}, .exits = std::move(initial.exits)};
    if (!initial.normal) {
        co_return result;
    }
    auto head = std::move(initial.normal->state);
    scan_writes(head, *source.body);
    scan_writes(head, *source.steps);
    if (source.condition) {
        scan_writes(head, *source.condition);
    }
    auto branches = source.condition
        ? (co_await condition(*source.condition, std::move(head)))
        : NullCondition {
              .yes = NullNormal {.state = std::move(head), .value = {}},
              .no = {},
              .exits = {}
          };
    result.normal = std::move(branches.no);
    append_null_exits(result.exits, std::move(branches.exits));
    if (!branches.yes) {
        co_return result;
    }
    auto iteration = (co_await region(*source.body, std::move(branches.yes->state)));
    auto step_input = std::move(iteration.normal);
    for (auto& exit : iteration.exits) {
        const auto* transfer = std::get_if<NullTransfer>(&exit.payload);
        if (transfer != nullptr && *transfer == NullTransfer::Break) {
            join_null_normal(
                result.normal,
                NullNormal {.state = std::move(exit.state), .value = {}}
            );
        } else if (transfer != nullptr && *transfer == NullTransfer::Continue) {
            join_null_normal(step_input, NullNormal {.state = std::move(exit.state), .value = {}});
        } else {
            result.exits.push_back(std::move(exit));
        }
    }
    if (step_input) {
        auto steps = (co_await region(*source.steps, std::move(step_input->state)));
        append_null_exits(result.exits, std::move(steps.exits));
    }
    co_return result;
}

auto NullabilityBodyAnalyzer::range(const SemRangeLoop& source, NullState state) noexcept
    -> ContinuationTask<NullFlow> {
    auto initial = (co_await expression(source.source, std::move(state)));
    auto result = NullFlow {.normal = {}, .exits = std::move(initial.exits)};
    if (!initial.normal) {
        co_return result;
    }
    auto head = std::move(initial.normal->state);
    const auto previous_aliases = range_aliases;
    add_range_aliases(source);
    if (source.access == AccessMode::Write) {
        invalidate(head, location(source.source, true));
    }
    if (source.binding) {
        invalidate(head, NullPlace {.root = *source.binding, .path = {}});
    }
    scan_writes(head, *source.body);
    // The zero-iteration/normal exhausted edge uses the pre-invalidated state.
    result.normal = NullNormal {.state = head, .value = {}};
    auto iteration = (co_await region(*source.body, std::move(head)));
    for (auto& exit : iteration.exits) {
        const auto* transfer = std::get_if<NullTransfer>(&exit.payload);
        if (transfer != nullptr && *transfer == NullTransfer::Break) {
            join_null_normal(
                result.normal,
                NullNormal {.state = std::move(exit.state), .value = {}}
            );
        } else if (transfer == nullptr || *transfer != NullTransfer::Continue) {
            result.exits.push_back(std::move(exit));
        }
    }
    range_aliases = previous_aliases;
    co_return result;
}
