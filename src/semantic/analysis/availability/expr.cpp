module carven:semantic.analysis.availability.expr.impl;

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

auto BodyAvailabilityGraphBuilder::lower_conditional(
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
auto BodyAvailabilityGraphBuilder::lower_match(
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

auto BodyAvailabilityGraphBuilder::lower_try(
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

auto BodyAvailabilityGraphBuilder::lower_expression(
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
                for (auto field = value.fields.rbegin(); field != value.fields.rend(); ++field) {
                    entry = lower_expression(field->value, entry, targets);
                }
                return entry;
            },
            [&](const HIRCaseConstructionExpr& value) noexcept {
                auto entry = after_access;
                for (auto child = value.payload.rbegin(); child != value.payload.rend(); ++child) {
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
                for (auto argument = value.arguments.rbegin(); argument != value.arguments.rend();
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
                return lower_conditional(value.branches, value.else_branch, after_access, targets);
            },
            [&](const HIRMatchExpr& value) noexcept {
                return lower_match(value.subject, value.arms, after_access, targets);
            },
            [&](const HIRTryExpr& value) noexcept {
                return lower_try(id, value, after_access, targets);
            },
        },
        hir.expression(id).value
    );
}
