module carven:semantic.analysis.availability.stmt.impl;

import :semantic.analysis.availability.build;
import :semantic.analysis.availability.graph;
import :semantic.analysis.availability.place;
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

auto BodyAvailabilityGraphBuilder::lower_while(
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
auto BodyAvailabilityGraphBuilder::lower_c_style_for(
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

auto BodyAvailabilityGraphBuilder::lower_range_for(
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

auto BodyAvailabilityGraphBuilder::lower_statement(
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
                const auto direct = value.op == HIRAssignmentOperator::Assign && owner.has_value();
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
            [&](const HIRWhileStmt& value) noexcept { return lower_while(value, normal, targets); },
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

auto BodyAvailabilityGraphBuilder::lower_block(
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
