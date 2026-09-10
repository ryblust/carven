module carven:semantic.analysis.nullability.control.impl;

import :semantic.analysis.nullability.context;
import :support.invariant;
import :support.visit;
import std;

auto NullabilityBodyAnalyzer::region(const SemanticRegion& source, NullState state) noexcept
    -> NullFlow {
    auto flow =
        NullFlow {.normal = NullNormal {.state = std::move(state), .value = {}}, .exits = {}};
    for (const auto& item : source.statements) {
        if (!flow.normal) {
            break;
        }
        auto next = statement(item, std::move(flow.normal->state));
        flow.normal = std::move(next.normal);
        append_null_exits(flow.exits, std::move(next.exits));
    }
    if (flow.normal && source.result) {
        auto next = expression(*source.result, std::move(flow.normal->state));
        flow.normal = std::move(next.normal);
        append_null_exits(flow.exits, std::move(next.exits));
    }
    return flow;
}

auto NullabilityBodyAnalyzer::statement(const SemanticStatement& source, NullState state) noexcept
    -> NullFlow {
    auto flow =
        NullFlow {.normal = NullNormal {.state = std::move(state), .value = {}}, .exits = {}};
    const auto evaluate = [&](const SemanticExpression& value) noexcept {
        if (!flow.normal) {
            return;
        }
        auto next = expression(value, std::move(flow.normal->state));
        flow.normal = std::move(next.normal);
        append_null_exits(flow.exits, std::move(next.exits));
    };
    const auto transfer = [&](NullExitPayload payload) noexcept {
        if (flow.normal) {
            flow.exits.push_back({.payload = payload, .state = std::move(flow.normal->state)});
            flow.normal.reset();
        }
    };
    std::visit(
        Overloaded {
            [&](const SemReturn& value) noexcept {
                if (value.value) {
                    evaluate(*value.value);
                }
                transfer(NullTransfer::Return);
            },
            [&](const SemBreak&) noexcept { transfer(NullTransfer::Break); },
            [&](const SemContinue&) noexcept { transfer(NullTransfer::Continue); },
            [&](const SemRethrow&) noexcept {
                if (flow.normal) {
                    for (const auto type : caught) {
                        flow.exits.push_back({.payload = type, .state = flow.normal->state});
                    }
                    flow.normal.reset();
                }
            },
            [&](const SemThrow& value) noexcept {
                evaluate(value.value);
                transfer(value.failure_type);
            },
            [&](const SemExpressionStatement& value) noexcept { evaluate(value.expression); },
            [&](const SemInitialize& value) noexcept {
                evaluate(value.initializer);
                if (flow.normal) {
                    const auto facts = flow.normal->value;
                    store(flow.normal->state, {.root = value.binding, .path = {}}, facts);
                }
            },
            [&](const SemAssign& value) noexcept {
                evaluate(value.target);
                evaluate(value.value);
                if (!flow.normal) {
                    return;
                }
                const auto facts = flow.normal->value;
                if (const auto target = location(value.target)) {
                    store(flow.normal->state, *target, facts);
                } else {
                    invalidate(flow.normal->state, location(value.target, true));
                }
            },
            [&](const SemLoop& value) noexcept {
                flow = loop(value, std::move(flow.normal->state));
            },
            [&](const SemRangeLoop& value) noexcept {
                flow = range(value, std::move(flow.normal->state));
            },
            [&](const SemTestReport& value) noexcept {
                // Assertions are consumers of bool, not proof-producing conditions.
                if (value.condition) {
                    evaluate(*value.condition);
                }
                if (value.message) {
                    evaluate(*value.message);
                }
                if (value.kind == TestReportKind::Fail) {
                    transfer(NullTransfer::Return);
                }
            },
            [&](const OwnedSemanticRegion& value) noexcept {
                flow = region(*value, std::move(flow.normal->state));
            },
        },
        source.value
    );
    if (flow.normal) {
        flow.normal->value = {};
    }
    return flow;
}

auto NullabilityBodyAnalyzer::conditional(const SemIf& source, NullState state) noexcept
    -> NullFlow {
    auto remaining = std::optional(NullNormal {.state = std::move(state), .value = {}});
    auto result = NullFlow();
    for (const auto& branch : source.branches) {
        if (!remaining) {
            break;
        }
        auto checked = condition(branch.condition, std::move(remaining->state));
        remaining = std::move(checked.no);
        append_null_exits(result.exits, std::move(checked.exits));
        if (checked.yes) {
            auto selected = region(branch.body, std::move(checked.yes->state));
            join_null_normal(result.normal, selected.normal);
            append_null_exits(result.exits, std::move(selected.exits));
        }
    }
    if (remaining && source.otherwise) {
        auto selected = region(**source.otherwise, std::move(remaining->state));
        join_null_normal(result.normal, selected.normal);
        append_null_exits(result.exits, std::move(selected.exits));
    } else {
        join_null_normal(result.normal, remaining);
    }
    return result;
}

auto NullabilityBodyAnalyzer::bind_pattern(
    NullState& state,
    PatternID id,
    const NullValue& value
) const noexcept -> void {
    std::visit(
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
        },
        body.pattern(id).value
    );
}

auto NullabilityBodyAnalyzer::irrefutable(PatternID id) const noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const WildcardPattern&) static noexcept { return true; },
            [](const BindingPattern&) static noexcept { return true; },
            [](const TypeConstraintPattern&) static noexcept { return true; },
            [&](const OrPattern& pattern) noexcept {
                return std::ranges::any_of(pattern.alternatives, [&](PatternID child) noexcept {
                    return irrefutable(child);
                });
            },
            [](const auto&) static noexcept { return false; },
        },
        body.pattern(id).value
    );
}

auto NullabilityBodyAnalyzer::match(const SemMatch& source, NullState state) noexcept -> NullFlow {
    auto subject = expression(*source.subject, std::move(state));
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
        if (irrefutable(arm.pattern)) {
            remaining.reset();
        }
        auto accepted = std::optional(std::move(selected));
        if (arm.guard) {
            auto checked = condition(*arm.guard, std::move(accepted->state));
            accepted = std::move(checked.yes);
            join_null_normal(remaining, checked.no);
            append_null_exits(result.exits, std::move(checked.exits));
        }
        if (accepted) {
            auto branch = region(arm.body, std::move(accepted->state));
            join_null_normal(result.normal, branch.normal);
            append_null_exits(result.exits, std::move(branch.exits));
        }
    }
    return result;
}

auto NullabilityBodyAnalyzer::attempt(const SemTry& source, NullState state) noexcept -> NullFlow {
    auto protected_flow = region(*source.body, std::move(state));
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
            auto exhaustive = false;
            for (const auto& alternative : arm.alternatives) {
                if (!alternative.reachable) {
                    continue;
                }
                if (std::holds_alternative<CatchAllPattern>(alternative.pattern)) {
                    exhaustive = true;
                } else if (const auto* typed =
                               std::get_if<SemTypedCatchPattern>(&alternative.pattern);
                           typed->type.resolved() == *type) {
                    exhaustive |= irrefutable(typed->inner);
                    bind_pattern(accepted->state, typed->inner, {});
                }
            }
            auto remaining = exhaustive ? std::optional<NullNormal>() : accepted;
            const auto previous = caught;
            caught = {*type};
            if (arm.guard) {
                auto checked = condition(*arm.guard, std::move(accepted->state));
                accepted = std::move(checked.yes);
                join_null_normal(remaining, checked.no);
                append_null_exits(result.exits, std::move(checked.exits));
            }
            if (accepted) {
                auto handled = region(arm.body, std::move(accepted->state));
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
    return result;
}

auto NullabilityBodyAnalyzer::loop(const SemLoop& source, NullState state) noexcept -> NullFlow {
    auto initial = region(*source.initializer, std::move(state));
    auto result = NullFlow {.normal = {}, .exits = std::move(initial.exits)};
    if (!initial.normal) {
        return result;
    }
    auto head = std::move(initial.normal->state);
    scan_writes(head, *source.body);
    scan_writes(head, *source.steps);
    if (source.condition) {
        scan_writes(head, *source.condition);
    }
    auto branches = source.condition
        ? condition(*source.condition, std::move(head))
        : NullCondition {
              .yes = NullNormal {.state = std::move(head), .value = {}},
              .no = {},
              .exits = {}
          };
    result.normal = std::move(branches.no);
    append_null_exits(result.exits, std::move(branches.exits));
    if (!branches.yes) {
        return result;
    }
    auto iteration = region(*source.body, std::move(branches.yes->state));
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
        auto steps = region(*source.steps, std::move(step_input->state));
        append_null_exits(result.exits, std::move(steps.exits));
    }
    return result;
}

auto NullabilityBodyAnalyzer::range(const SemRangeLoop& source, NullState state) noexcept
    -> NullFlow {
    auto initial = std::visit(
        Overloaded {
            [&](const SemIntegerRange& range) noexcept {
                auto begin = expression(range.begin, std::move(state));
                if (!begin.normal) {
                    return begin;
                }
                auto end = expression(range.end, std::move(begin.normal->state));
                append_null_exits(end.exits, std::move(begin.exits));
                return end;
            },
            [&](const SemSequenceRange& range) noexcept {
                return expression(range.value, std::move(state));
            },
        },
        source.source
    );
    auto result = NullFlow {.normal = {}, .exits = std::move(initial.exits)};
    if (!initial.normal) {
        return result;
    }
    auto head = std::move(initial.normal->state);
    const auto previous_aliases = range_aliases;
    add_range_aliases(source);
    if (source.access == AccessMode::Write) {
        if (const auto* sequence = std::get_if<SemSequenceRange>(&source.source)) {
            invalidate(head, location(sequence->value, true));
        }
    }
    if (source.binding) {
        invalidate(head, NullPlace {.root = *source.binding, .path = {}});
    }
    scan_writes(head, *source.body);
    // The zero-iteration/normal exhausted edge uses the pre-invalidated state.
    result.normal = NullNormal {.state = head, .value = {}};
    auto iteration = region(*source.body, std::move(head));
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
    return result;
}
