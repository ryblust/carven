module carven:semantic.analysis.control.evaluator.impl;

import :semantic.analysis.control;
import :semantic.analysis.control.internal;
import :semantic.analysis.coverage;
import :semantic.analysis.failures;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct ConcreteCall final {
    CallableID callable;
};

struct SignatureCall final {
    CallableSignatureID signature;
};

struct NonFailingCall final {};

using ResolvedCallTarget = std::variant<ConcreteCall, SignatureCall, NonFailingCall>;

auto empty_control() noexcept -> ControlSummary {
    return {
        .falls_through = true,
        .transfers =
            {
                .returns = false,
                .breaks = false,
                .continues = false,
                .exits_test = false,
            },
        .pending_failures = {},
        .outward_failures = {},
    };
}

auto unreachable_control() noexcept -> ControlSummary {
    auto result = empty_control();
    result.falls_through = false;
    return result;
}

auto append_failures(
    std::vector<HIRTypeID>& destination,
    std::span<const HIRTypeID> source
) noexcept -> void {
    destination.insert(destination.end(), source.begin(), source.end());
}

auto match_exhaustive(
    const SemanticConstruction& hir,
    HIRTypeID subject,
    std::span<const HIRMatchArm> arms
) noexcept -> bool {
    const auto coverage = compute_pattern_coverage(hir, subject, arms);
    if (!coverage.has_value()) {
        invariant_violation("validated pattern coverage could not be recomputed");
    }
    return coverage->exhaustive;
}

auto catch_type_exhaustive(
    const SemanticConstruction& hir,
    HIRTypeID failure,
    std::span<const HIRCatchArm> arms
) noexcept -> bool;

auto catch_arm_reachable(
    const SemanticConstruction& hir,
    HIRTypeID failure,
    std::span<const HIRCatchArm> arms
) noexcept -> bool {
    auto patterns = std::vector<HIRMatchArm>();
    auto owners = std::vector<std::size_t>();
    for (auto arm_index = 0uz; arm_index < arms.size(); ++arm_index) {
        const auto& arm = arms[arm_index];
        for (const auto& alternative : arm.alternatives) {
            if (alternative.type.has_value() && *alternative.type != failure) {
                continue;
            }
            if (!alternative.inner.has_value()) {
                if (arm_index + 1 == arms.size()) {
                    return !catch_type_exhaustive(hir, failure, arms.first(arm_index));
                }
                if (!arm.guard.has_value()) {
                    return false;
                }
                continue;
            }
            patterns.push_back({
                .scope = arm.scope,
                .pattern = *alternative.inner,
                .guard = arm.guard,
                .body = arm.body,
            });
            owners.push_back(arm_index);
        }
    }
    const auto coverage = compute_pattern_coverage(hir, failure, patterns);
    if (!coverage.has_value()) {
        invariant_violation("validated catch coverage could not be recomputed");
    }
    for (auto index = 0uz; index < coverage->arm_usefulness.size(); ++index) {
        if (owners[index] + 1 == arms.size() && coverage->arm_usefulness[index]) {
            return true;
        }
    }
    return false;
}

auto catch_type_exhaustive(
    const SemanticConstruction& hir,
    HIRTypeID failure,
    std::span<const HIRCatchArm> arms
) noexcept -> bool {
    auto patterns = std::vector<HIRMatchArm>();
    for (const auto& arm : arms) {
        for (const auto& alternative : arm.alternatives) {
            if (alternative.type.has_value() && *alternative.type != failure) {
                continue;
            }
            if (!alternative.inner.has_value()) {
                if (!arm.guard.has_value()) {
                    return true;
                }
                continue;
            }
            patterns.push_back({
                .scope = arm.scope,
                .pattern = *alternative.inner,
                .guard = arm.guard,
                .body = arm.body,
            });
        }
    }
    return match_exhaustive(hir, failure, patterns);
}

class ControlEvaluator final {
public:
    ControlEvaluator(
        const SemanticConstruction& hir,
        const std::vector<std::vector<HIRTypeID>>& failure_sets,
        std::vector<std::vector<std::uint32_t>>* dependencies,
        RecordedControl* facts
    ) noexcept
        : hir(hir),
          failure_sets(failure_sets),
          dependencies(dependencies),
          facts(facts) {}

    auto evaluate_callable(CallableID callable) noexcept -> ControlSummary {
        const auto previous = active_callable;
        active_callable = callable;
        const auto result = evaluate_block(hir.body(hir.callable(callable).body).root);
        active_callable = previous;
        return result;
    }

    auto evaluate_test(TestID test) noexcept -> ControlSummary {
        return evaluate_block(hir.body(hir.test(test).body).root);
    }

private:
    auto normalize(ControlSummary value) const noexcept -> ControlSummary {
        value.pending_failures = normalize_failure_members(hir, std::move(value.pending_failures));
        value.outward_failures = normalize_failure_members(hir, std::move(value.outward_failures));
        return value;
    }

    auto sequence(ControlSummary first, const ControlSummary& next) const noexcept
        -> ControlSummary {
        if (!first.falls_through) {
            return first;
        }
        first.falls_through = next.falls_through;
        first.transfers.returns |= next.transfers.returns;
        first.transfers.breaks |= next.transfers.breaks;
        first.transfers.continues |= next.transfers.continues;
        first.transfers.exits_test |= next.transfers.exits_test;
        append_failures(first.pending_failures, next.pending_failures);
        append_failures(first.outward_failures, next.outward_failures);
        return normalize(std::move(first));
    }

    auto alternatives(std::span<const ControlSummary> paths) const noexcept -> ControlSummary {
        auto result = unreachable_control();
        for (const auto& path : paths) {
            result.falls_through |= path.falls_through;
            result.transfers.returns |= path.transfers.returns;
            result.transfers.breaks |= path.transfers.breaks;
            result.transfers.continues |= path.transfers.continues;
            result.transfers.exits_test |= path.transfers.exits_test;
            append_failures(result.pending_failures, path.pending_failures);
            append_failures(result.outward_failures, path.outward_failures);
        }
        return normalize(std::move(result));
    }

    auto resolved_call_target(HIRExprID callee) const noexcept -> ResolvedCallTarget {
        const auto& type = hir.type(hir.expression(callee).type).value;
        if (const auto* function = std::get_if<HIRFunctionTypeValue>(&type)) {
            return ConcreteCall {.callable = function->callable};
        }
        if (const auto* closure = std::get_if<HIRClosureTypeValue>(&type)) {
            return ConcreteCall {.callable = closure->callable};
        }
        if (const auto* reference = std::get_if<HIRFunctionRefTypeValue>(&type)) {
            return SignatureCall {.signature = reference->signature};
        }
        return NonFailingCall {};
    }

    auto call_failures(const ResolvedCallTarget& target) const noexcept
        -> std::span<const HIRTypeID> {
        return std::visit(
            Overloaded {
                [&](const ConcreteCall& value) noexcept -> std::span<const HIRTypeID> {
                    return failure_sets[value.callable.index()];
                },
                [&](const SignatureCall& value) noexcept -> std::span<const HIRTypeID> {
                    return hir.failure_set(hir.callable_signature(value.signature).failure_set)
                        .members;
                },
                [](const NonFailingCall&) static noexcept -> std::span<const HIRTypeID> {
                    return {};
                },
            },
            target
        );
    }

    auto record_dependency(const ResolvedCallTarget& target) noexcept -> void {
        if (dependencies == nullptr || !active_callable.has_value()) {
            return;
        }
        const auto* concrete = std::get_if<ConcreteCall>(&target);
        if (concrete == nullptr) {
            return;
        }
        auto& targets = (*dependencies)[active_callable->index()];
        if (!std::ranges::contains(targets, concrete->callable.index())) {
            targets.push_back(concrete->callable.index());
        }
    }

    auto evaluate_conditional(
        std::span<const HIRConditionalBranch> branches,
        std::optional<HIRBlockID> other
    ) noexcept -> ControlSummary {
        auto paths = std::vector<ControlSummary>();
        for (const auto& branch : branches) {
            auto path = evaluate_expression(branch.condition);
            path = sequence(std::move(path), evaluate_block(branch.body));
            paths.push_back(std::move(path));
        }
        paths.push_back(other.has_value() ? evaluate_block(*other) : empty_control());
        return alternatives(paths);
    }

    auto evaluate_match(HIRExprID subject, std::span<const HIRMatchArm> arms) noexcept
        -> ControlSummary {
        auto result = evaluate_expression(subject);
        auto paths = std::vector<ControlSummary>();
        for (const auto& arm : arms) {
            auto path = arm.guard.has_value() ? evaluate_expression(*arm.guard) : empty_control();
            path = sequence(std::move(path), evaluate_block(arm.body));
            paths.push_back(std::move(path));
        }
        return sequence(std::move(result), alternatives(paths));
    }

    auto catch_types(
        const HIRCatchArm& arm,
        std::span<const HIRTypeID> protected_failures
    ) const noexcept -> std::vector<HIRTypeID> {
        auto result = std::vector<HIRTypeID>();
        for (const auto& alternative : arm.alternatives) {
            if (alternative.type.has_value()) {
                if (std::ranges::contains(protected_failures, *alternative.type)) {
                    result.push_back(*alternative.type);
                }
            } else {
                append_failures(result, protected_failures);
            }
        }
        return normalize_failure_members(hir, std::move(result));
    }

    auto evaluate_try(HIRExprID id, const HIRTryExpr& attempt) noexcept -> ControlSummary {
        const auto protected_control = evaluate_block(attempt.body);
        auto normal = protected_control;
        normal.outward_failures.clear();
        auto paths = std::vector<ControlSummary> {std::move(normal)};
        auto remaining = protected_control.outward_failures;
        auto outward = std::vector<HIRTypeID>();
        auto handler_pending = std::vector<HIRTypeID>();
        auto accepted = std::vector<std::vector<HIRTypeID>>(attempt.arms.size());

        for (auto arm_index = 0uz; arm_index < attempt.arms.size(); ++arm_index) {
            const auto& arm = attempt.arms[arm_index];
            auto matched = catch_types(arm, remaining);
            const auto prefix = std::span(attempt.arms).first(arm_index + 1);
            std::erase_if(matched, [&](HIRTypeID failure) noexcept {
                return !catch_arm_reachable(hir, failure, prefix);
            });
            accepted[arm_index] = matched;
            const auto previous_rethrow = rethrow_failures;
            rethrow_failures = matched;
            auto arm_control =
                arm.guard.has_value() ? evaluate_expression(*arm.guard) : empty_control();
            arm_control = sequence(std::move(arm_control), evaluate_block(arm.body));
            rethrow_failures = previous_rethrow;
            if (matched.empty()) {
                continue;
            }
            append_failures(handler_pending, arm_control.pending_failures);
            append_failures(outward, arm_control.outward_failures);
            arm_control.pending_failures.clear();
            arm_control.outward_failures.clear();
            paths.push_back(std::move(arm_control));
            std::erase_if(remaining, [&](HIRTypeID failure) noexcept {
                return catch_type_exhaustive(hir, failure, prefix);
            });
        }
        if (facts != nullptr) {
            facts->catches[id.index()] = std::move(accepted);
            facts->unhandled[id.index()] = remaining;
        }
        append_failures(outward, remaining);
        auto result = alternatives(paths);
        result.pending_failures = protected_control.pending_failures;
        append_failures(result.pending_failures, handler_pending);
        append_failures(result.outward_failures, outward);
        return normalize(std::move(result));
    }

    auto evaluate_expression(HIRExprID id) noexcept -> ControlSummary {
        auto result = empty_control();
        const auto merge_child = [&](const ControlSummary& child) noexcept {
            result = sequence(std::move(result), child);
        };
        std::visit(
            Overloaded {
                [](const HIRLiteralExpr&) static noexcept {},
                [](const HIRNameExpr&) static noexcept {},
                [&](const HIRArrayExpr& value) noexcept {
                    for (const auto child : value.element_ids) {
                        merge_child(evaluate_expression(child));
                    }
                },
                [&](const HIRConstructionExpr& value) noexcept {
                    for (const auto& field : value.fields) {
                        merge_child(evaluate_expression(field.value));
                    }
                },
                [&](const HIRCaseConstructionExpr& value) noexcept {
                    for (const auto child : value.payload) {
                        merge_child(evaluate_expression(child));
                    }
                },
                [&](const HIRUnaryExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                },
                [&](const HIRBinaryExpr& value) noexcept {
                    merge_child(evaluate_expression(value.left));
                    merge_child(evaluate_expression(value.right));
                },
                [&](const HIRCastExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                },
                [&](const HIRCallExpr& value) noexcept {
                    merge_child(evaluate_expression(value.callee));
                    for (const auto& argument : value.arguments) {
                        merge_child(evaluate_expression(argument.expression));
                    }
                    const auto target = resolved_call_target(value.callee);
                    record_dependency(target);
                    append_failures(result.pending_failures, call_failures(target));
                },
                [](const HIRClosureExpr&) static noexcept {},
                [&](const HIRCallableViewExpr& value) noexcept {
                    merge_child(evaluate_expression(value.source));
                },
                [&](const HIRPropagationExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                    append_failures(result.outward_failures, result.pending_failures);
                    result.pending_failures.clear();
                },
                [&](const HIRTakeExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                },
                [&](const HIRTextIntrinsicExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                },
                [&](const HIRIndexExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                    merge_child(evaluate_expression(value.index));
                },
                [&](const HIRMemberExpr& value) noexcept {
                    merge_child(evaluate_expression(value.operand_id));
                },
                [&](const HIRIfExpr& value) noexcept {
                    result = evaluate_conditional(value.branches, value.else_branch);
                },
                [&](const HIRMatchExpr& value) noexcept {
                    result = evaluate_match(value.subject, value.arms);
                },
                [&](const HIRTryExpr& value) noexcept { result = evaluate_try(id, value); },
                [](const HIRCppExpr&) static noexcept {},
            },
            hir.expression(id).value
        );
        result = normalize(std::move(result));
        if (facts != nullptr) {
            facts->expressions[id.index()] = result;
        }
        return result;
    }

    auto evaluate_statement(HIRStmtID id) noexcept -> ControlSummary {
        auto result = std::visit(
            Overloaded {
                [&](const HIRReturnStmt& value) noexcept {
                    auto control = value.value.has_value() ? evaluate_expression(*value.value)
                                                           : empty_control();
                    if (control.falls_through) {
                        control.transfers.returns = true;
                    }
                    control.falls_through = false;
                    return control;
                },
                [](const HIRBreakStmt&) static noexcept {
                    auto control = unreachable_control();
                    control.transfers.breaks = true;
                    return control;
                },
                [](const HIRContinueStmt&) static noexcept {
                    auto control = unreachable_control();
                    control.transfers.continues = true;
                    return control;
                },
                [&](const HIRThrowStmt& value) noexcept {
                    auto control = evaluate_expression(value.value);
                    control.falls_through = false;
                    control.outward_failures.push_back(value.failure_type);
                    return normalize(std::move(control));
                },
                [&](const HIRRethrowStmt&) noexcept {
                    auto control = unreachable_control();
                    if (!rethrow_failures.has_value()) {
                        invariant_violation("validated rethrow has no enclosing catch source");
                    }
                    control.outward_failures = *rethrow_failures;
                    return control;
                },
                [&](const HIRExprStmt& value) noexcept {
                    return evaluate_expression(value.expression);
                },
                [&](const HIRBindingStmt& value) noexcept {
                    return evaluate_expression(value.initializer);
                },
                [&](const HIRAssignmentStmt& value) noexcept {
                    auto control = evaluate_expression(value.target);
                    return sequence(std::move(control), evaluate_expression(value.value));
                },
                [&](const HIRUpdateStmt& value) noexcept {
                    return evaluate_expression(value.target);
                },
                [&](const HIRIfStmt& value) noexcept {
                    return evaluate_conditional(value.branches, value.else_branch);
                },
                [&](const HIRMatchStmt& value) noexcept {
                    return evaluate_match(value.subject, value.arms);
                },
                [&](const HIRWhileStmt& value) noexcept {
                    auto control = evaluate_expression(value.condition);
                    const auto body = evaluate_block(value.body);
                    control = sequence(std::move(control), body);
                    auto condition_true = false;
                    if (const auto* literal =
                            std::get_if<HIRLiteralExpr>(&hir.expression(value.condition).value)) {
                        if (const auto* boolean =
                                std::get_if<HIRBooleanLiteralValue>(&literal->value)) {
                            condition_true = boolean->value;
                        }
                    }
                    control.falls_through = !condition_true || body.transfers.breaks;
                    control.transfers.breaks = false;
                    control.transfers.continues = false;
                    return control;
                },
                [&](const HIRCStyleForStmt& value) noexcept {
                    auto control = value.initializer.has_value()
                        ? evaluate_statement(*value.initializer)
                        : empty_control();
                    if (value.condition.has_value()) {
                        control =
                            sequence(std::move(control), evaluate_expression(*value.condition));
                    }
                    auto body = evaluate_block(value.body);
                    body.falls_through |= body.transfers.continues;
                    body.transfers.continues = false;
                    control = sequence(std::move(control), body);
                    for (const auto step : value.steps) {
                        control = sequence(std::move(control), evaluate_statement(step));
                    }
                    control.falls_through = value.condition.has_value() || body.transfers.breaks;
                    control.transfers.breaks = false;
                    control.transfers.continues = false;
                    return control;
                },
                [&](const HIRRangeForStmt& value) noexcept {
                    auto control = empty_control();
                    std::visit(
                        Overloaded {
                            [&](HIRExprID expression) noexcept {
                                control =
                                    sequence(std::move(control), evaluate_expression(expression));
                            },
                            [&](const HIRHalfOpenRange& range) noexcept {
                                control =
                                    sequence(std::move(control), evaluate_expression(range.begin));
                                control =
                                    sequence(std::move(control), evaluate_expression(range.end));
                            },
                        },
                        value.iterable
                    );
                    control = sequence(std::move(control), evaluate_block(value.body));
                    control.falls_through = true;
                    control.transfers.breaks = false;
                    control.transfers.continues = false;
                    return control;
                },
                [&](const HIRTestCheckStmt& value) noexcept {
                    auto control = evaluate_expression(value.condition);
                    if (value.message.has_value()) {
                        control = sequence(std::move(control), evaluate_expression(*value.message));
                    }
                    return control;
                },
                [&](const HIRTestRequireStmt& value) noexcept {
                    auto control = evaluate_expression(value.condition);
                    if (value.message.has_value()) {
                        control = sequence(std::move(control), evaluate_expression(*value.message));
                    }
                    control.transfers.exits_test = true;
                    return control;
                },
                [&](const HIRTestFailStmt& value) noexcept {
                    auto control = value.message.has_value() ? evaluate_expression(*value.message)
                                                             : empty_control();
                    control.falls_through = false;
                    control.transfers.exits_test = true;
                    return control;
                },
                [](const HIRCppStmt&) static noexcept { return empty_control(); },
            },
            hir.statement(id).value
        );
        result = normalize(std::move(result));
        if (facts != nullptr) {
            facts->statements[id.index()] = result;
        }
        return result;
    }

    auto evaluate_block(HIRBlockID id) noexcept -> ControlSummary {
        auto result = empty_control();
        const auto& block = hir.block(id);
        for (const auto statement : block.statements) {
            result = sequence(std::move(result), evaluate_statement(statement));
        }
        if (block.result.has_value()) {
            result = sequence(std::move(result), evaluate_expression(*block.result));
        }
        if (facts != nullptr) {
            facts->blocks[id.index()] = result;
        }
        return result;
    }

    const SemanticConstruction& hir;
    const std::vector<std::vector<HIRTypeID>>& failure_sets;
    std::vector<std::vector<std::uint32_t>>* dependencies;
    RecordedControl* facts;
    std::optional<CallableID> active_callable;
    std::optional<std::vector<HIRTypeID>> rethrow_failures;
};

} // namespace

auto evaluate_callable_control(
    const SemanticConstruction& hir,
    const std::vector<std::vector<HIRTypeID>>& failure_sets,
    CallableID callable,
    std::vector<std::vector<std::uint32_t>>* dependencies
) noexcept -> ControlSummary {
    return ControlEvaluator(hir, failure_sets, dependencies, nullptr).evaluate_callable(callable);
}

auto record_control(
    const SemanticConstruction& hir,
    const std::vector<std::vector<HIRTypeID>>& failure_sets
) noexcept -> RecordedControl {
    auto recorded = RecordedControl {
        .expressions = std::vector<ControlSummary>(hir.expressions().size(), empty_control()),
        .statements = std::vector<ControlSummary>(hir.statements().size(), empty_control()),
        .blocks = std::vector<ControlSummary>(hir.blocks().size(), empty_control()),
        .catches = std::vector<std::vector<std::vector<HIRTypeID>>>(hir.expressions().size()),
        .unhandled = std::vector<std::vector<HIRTypeID>>(hir.expressions().size()),
    };
    auto evaluator = ControlEvaluator(hir, failure_sets, nullptr, &recorded);
    for (auto index = 0uz; index < hir.callables().size(); ++index) {
        evaluator.evaluate_callable(CallableID::from_index(static_cast<std::uint32_t>(index)));
    }
    for (auto index = 0uz; index < hir.tests().size(); ++index) {
        evaluator.evaluate_test(TestID::from_index(static_cast<std::uint32_t>(index)));
    }
    return recorded;
}
