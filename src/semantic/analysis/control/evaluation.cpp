module carven:semantic.analysis.control.evaluation.impl;

import :semantic.analysis.control.internal;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :support.visit;
import std;

namespace {

auto merge_roots(std::vector<SymbolID>& destination, std::span<const SymbolID> source) noexcept
    -> void {
    auto merged = std::vector<SymbolID>();
    merged.reserve(destination.size() + source.size());
    std::ranges::set_union(destination, source, std::back_inserter(merged));
    destination = std::move(merged);
}

auto merge_effect(EvaluationEffect& destination, const EvaluationEffect& source) noexcept -> void {
    merge_roots(destination.reads, source.reads);
    merge_roots(destination.writes, source.writes);
    merge_roots(destination.takes, source.takes);
    destination.opaque_boundary |= source.opaque_boundary;
}

class EvaluationEffectEvaluator final {
public:
    explicit EvaluationEffectEvaluator(SemanticDraftView hir) noexcept
        : hir(hir),
          effects(hir.expressions().size()) {}

    auto evaluate_callable(CallableID callable) noexcept -> void {
        if (const auto body = callable_body_id(hir.callable(callable))) {
            evaluate_block(hir.body(*body).root);
        }
    }

    auto evaluate_test(TestID test) noexcept -> void {
        evaluate_block(hir.body(hir.test(test).body).root);
    }

    auto take_effects() noexcept -> std::vector<EvaluationEffect> { return std::move(effects); }

private:
    auto add_place_effect(HIRExprID id, EvaluationEffect& effect) const noexcept -> void {
        const auto& use = hir.place_use_candidate(id);
        if (!use.has_value()) {
            return;
        }
        auto* roots = &effect.reads;
        switch (use->access) {
            case SemanticPlaceAccess::Read:      roots = &effect.reads; break;
            case SemanticPlaceAccess::Write:
            case SemanticPlaceAccess::ReadWrite: roots = &effect.writes; break;
            case SemanticPlaceAccess::Take:      roots = &effect.takes; break;
        }
        const auto position = std::ranges::lower_bound(*roots, use->root);
        if (position == roots->end() || *position != use->root) {
            roots->insert(position, use->root);
        }
    }

    auto evaluate_expression(HIRExprID id) noexcept -> EvaluationEffect {
        auto result = EvaluationEffect {};
        const auto merge_expression = [&](HIRExprID child) noexcept {
            merge_effect(result, evaluate_expression(child));
        };
        const auto merge_block = [&](HIRBlockID block) noexcept {
            merge_effect(result, evaluate_block(block));
        };
        std::visit(
            Overloaded {
                [](const HIRLiteralExpr&) static noexcept {},
                [](const HIRNameExpr&) static noexcept {},
                [&](const HIRArrayExpr& value) noexcept {
                    for (const auto child : value.element_ids) {
                        merge_expression(child);
                    }
                },
                [&](const HIRConstructionExpr& value) noexcept {
                    for (const auto& field : value.fields) {
                        merge_expression(field.value);
                    }
                },
                [&](const HIRCaseConstructionExpr& value) noexcept {
                    for (const auto child : value.payload) {
                        merge_expression(child);
                    }
                },
                [&](const HIRUnaryExpr& value) noexcept { merge_expression(value.operand_id); },
                [&](const HIRBinaryExpr& value) noexcept {
                    merge_expression(value.left);
                    merge_expression(value.right);
                },
                [&](const HIRCastExpr& value) noexcept { merge_expression(value.operand_id); },
                [&](const HIRCallExpr& value) noexcept {
                    merge_expression(value.callee);
                    for (const auto& argument : value.arguments) {
                        merge_expression(argument.expression);
                    }
                },
                [](const HIRClosureExpr&) static noexcept {},
                [&](const HIRCallableViewExpr& value) noexcept { merge_expression(value.source); },
                [&](const HIRPropagationExpr& value) noexcept {
                    merge_expression(value.operand_id);
                },
                [&](const HIRTakeExpr& value) noexcept { merge_expression(value.operand_id); },
                [&](const HIRTextIntrinsicExpr& value) noexcept {
                    merge_expression(value.operand_id);
                },
                [&](const HIRIndexExpr& value) noexcept {
                    merge_expression(value.operand_id);
                    merge_expression(value.index);
                },
                [&](const HIRMemberExpr& value) noexcept { merge_expression(value.operand_id); },
                [&](const HIRIfExpr& value) noexcept {
                    for (const auto& branch : value.branches) {
                        merge_expression(branch.condition);
                        merge_block(branch.body);
                    }
                    if (value.else_branch.has_value()) {
                        merge_block(*value.else_branch);
                    }
                },
                [&](const HIRMatchExpr& value) noexcept {
                    merge_expression(value.subject);
                    for (const auto& arm : value.arms) {
                        if (arm.guard.has_value()) {
                            merge_expression(*arm.guard);
                        }
                        merge_block(arm.body);
                    }
                },
                [&](const HIRTryExpr& value) noexcept {
                    merge_block(value.body);
                    for (const auto& arm : value.arms) {
                        if (arm.guard.has_value()) {
                            merge_expression(*arm.guard);
                        }
                        merge_block(arm.body);
                    }
                },
            },
            hir.expression(id).value
        );
        add_place_effect(id, result);
        const auto& expression = hir.expression(id).value;
        result.opaque_boundary |= std::holds_alternative<HIRCallExpr>(expression)
            || std::holds_alternative<HIRClosureExpr>(expression)
            || std::holds_alternative<HIRIfExpr>(expression)
            || std::holds_alternative<HIRMatchExpr>(expression)
            || std::holds_alternative<HIRTryExpr>(expression);
        effects[id.index()] = result;
        return result;
    }

    auto evaluate_statement(HIRStmtID id) noexcept -> EvaluationEffect {
        auto result = EvaluationEffect {};
        const auto merge_expression = [&](HIRExprID expression) noexcept {
            merge_effect(result, evaluate_expression(expression));
        };
        const auto merge_block = [&](HIRBlockID block) noexcept {
            merge_effect(result, evaluate_block(block));
        };
        std::visit(
            Overloaded {
                [&](const HIRReturnStmt& value) noexcept {
                    if (value.value.has_value()) {
                        merge_expression(*value.value);
                    }
                },
                [](const HIRBreakStmt&) static noexcept {},
                [](const HIRContinueStmt&) static noexcept {},
                [&](const HIRThrowStmt& value) noexcept { merge_expression(value.value); },
                [](const HIRRethrowStmt&) static noexcept {},
                [&](const HIRExprStmt& value) noexcept { merge_expression(value.expression); },
                [&](const HIRBindingStmt& value) noexcept { merge_expression(value.initializer); },
                [&](const HIRAssignmentStmt& value) noexcept {
                    merge_expression(value.target);
                    merge_expression(value.value);
                },
                [&](const HIRUpdateStmt& value) noexcept { merge_expression(value.target); },
                [&](const HIRIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        merge_expression(branch.condition);
                        merge_block(branch.body);
                    }
                    if (value.else_branch.has_value()) {
                        merge_block(*value.else_branch);
                    }
                },
                [&](const HIRMatchStmt& value) noexcept {
                    merge_expression(value.subject);
                    for (const auto& arm : value.arms) {
                        if (arm.guard.has_value()) {
                            merge_expression(*arm.guard);
                        }
                        merge_block(arm.body);
                    }
                },
                [&](const HIRWhileStmt& value) noexcept {
                    merge_expression(value.condition);
                    merge_block(value.body);
                },
                [&](const HIRCStyleForStmt& value) noexcept {
                    if (value.initializer.has_value()) {
                        merge_effect(result, evaluate_statement(*value.initializer));
                    }
                    if (value.condition.has_value()) {
                        merge_expression(*value.condition);
                    }
                    merge_block(value.body);
                    for (const auto step : value.steps) {
                        merge_effect(result, evaluate_statement(step));
                    }
                },
                [&](const HIRRangeForStmt& value) noexcept {
                    std::visit(
                        Overloaded {
                            [&](HIRExprID expression) noexcept { merge_expression(expression); },
                            [&](const HIRHalfOpenRange& range) noexcept {
                                merge_expression(range.begin);
                                merge_expression(range.end);
                            },
                        },
                        value.iterable
                    );
                    merge_block(value.body);
                },
                [&](const HIRTestCheckStmt& value) noexcept {
                    merge_expression(value.condition);
                    if (value.message.has_value()) {
                        merge_expression(*value.message);
                    }
                },
                [&](const HIRTestRequireStmt& value) noexcept {
                    merge_expression(value.condition);
                    if (value.message.has_value()) {
                        merge_expression(*value.message);
                    }
                },
                [&](const HIRTestFailStmt& value) noexcept {
                    if (value.message.has_value()) {
                        merge_expression(*value.message);
                    }
                },
            },
            hir.statement(id).value
        );
        return result;
    }

    auto evaluate_block(HIRBlockID id) noexcept -> EvaluationEffect {
        auto result = EvaluationEffect {};
        const auto& block = hir.block(id);
        for (const auto statement : block.statements) {
            merge_effect(result, evaluate_statement(statement));
        }
        if (block.result.has_value()) {
            merge_effect(result, evaluate_expression(*block.result));
        }
        return result;
    }

    SemanticDraftView hir;
    std::vector<EvaluationEffect> effects;
};

} // namespace

auto derive_evaluation_effects(SemanticDraftView hir) noexcept -> std::vector<EvaluationEffect> {
    auto evaluator = EvaluationEffectEvaluator(hir);
    for (auto index = 0uz; index < hir.callables().size(); ++index) {
        evaluator.evaluate_callable(CallableID::from_index(static_cast<std::uint32_t>(index)));
    }
    for (auto index = 0uz; index < hir.tests().size(); ++index) {
        evaluator.evaluate_test(TestID::from_index(static_cast<std::uint32_t>(index)));
    }
    return evaluator.take_effects();
}
