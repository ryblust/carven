module carven:test.internal.backend.target.traversal;

import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.traversal;
import :test.harness.framework;
import std;

namespace {

auto attribution() noexcept -> TargetAttribution {
    return TargetGeneratedExpansionAttribution {
        .reason = TargetExpansionReason::LoweringSupport,
    };
}

auto literal(bool value) noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = value}};
}

auto one_statement(TargetStmt statement) noexcept -> std::vector<TargetStmt> {
    auto result = std::vector<TargetStmt>();
    result.push_back(std::move(statement));
    return result;
}

enum class TraversalObservation { EnterBlock, TrueValue, FalseValue, LeaveBlock };

struct SparseTraversal final {
    bool stop_at_false;
    std::vector<TraversalObservation> observations;

    auto enter_scope(TargetTraversalScope scope) noexcept -> bool {
        expect(scope == TargetTraversalScope::Block);
        observations.push_back(TraversalObservation::EnterBlock);
        return true;
    }

    auto leave_scope(TargetTraversalScope scope) noexcept -> bool {
        expect(scope == TargetTraversalScope::Block);
        observations.push_back(TraversalObservation::LeaveBlock);
        return true;
    }

    auto enter_expression(TargetExpr& expression, TargetExpressionRole role) noexcept -> bool {
        expect(role == TargetExpressionRole::Operand);
        auto* literal = std::get_if<TargetLiteralExpr>(&expression.value);
        if (!expect(literal != nullptr)) {
            return false;
        }
        auto* value = std::get_if<bool>(&literal->value);
        if (!expect(value != nullptr)) {
            return false;
        }
        const auto observed = *value;
        observations.push_back(
            observed ? TraversalObservation::TrueValue : TraversalObservation::FalseValue
        );
        *value = !*value;
        return observed || !stop_at_false;
    }
};

const TestSuite suite([] static noexcept {
    "Target traversal: sparse hooks preserve nested order, mutation, and early stop"_test =
        [] static noexcept {
            each(
                std::array {false, true},
                [](bool stop) static noexcept { return stop ? "early stop" : "complete"; },
                [](bool stop) static noexcept {
                    constexpr auto expression = [](bool value) static noexcept -> TargetStmt {
                        return {
                            .value = TargetExprStmt {.expression = literal(value)},
                            .attribution = attribution(),
                        };
                    };
                    auto nested = one_statement(expression(true));
                    nested.push_back(expression(false));
                    auto body = one_statement({
                        .value = TargetBlockStmt {.statements = std::move(nested)},
                        .attribution = attribution(),
                    });
                    body.push_back(expression(true));
                    auto visitor = SparseTraversal {.stop_at_false = stop, .observations = {}};
                    expect_equal(traverse_target_statements(body, visitor), !stop);
                    auto expected = std::vector {
                        TraversalObservation::EnterBlock,
                        TraversalObservation::TrueValue,
                        TraversalObservation::FalseValue,
                    };
                    if (!stop) {
                        expected.push_back(TraversalObservation::LeaveBlock);
                        expected.push_back(TraversalObservation::TrueValue);
                    }
                    expect(visitor.observations == expected);
                    constexpr auto boolean =
                        [](const TargetStmt& statement) static noexcept -> std::optional<bool> {
                        const auto* expression = std::get_if<TargetExprStmt>(&statement.value);
                        if (!expect(expression != nullptr)) {
                            return std::nullopt;
                        }
                        const auto* literal =
                            std::get_if<TargetLiteralExpr>(&expression->expression.value);
                        if (!expect(literal != nullptr)) {
                            return std::nullopt;
                        }
                        const auto* value = std::get_if<bool>(&literal->value);
                        if (!expect(value != nullptr)) {
                            return std::nullopt;
                        }
                        return *value;
                    };
                    const auto* block = std::get_if<TargetBlockStmt>(&body.front().value);
                    if (!expect(block != nullptr) || !expect_equal(block->statements.size(), 2uz)) {
                        return;
                    }
                    const auto first = boolean(block->statements[0]);
                    const auto second = boolean(block->statements[1]);
                    const auto last = boolean(body.back());
                    if (!first || !second || !last) {
                        return;
                    }
                    expect_equal(*first, false);
                    expect_equal(*second, true);
                    expect_equal(*last, stop);
                }
            );
        };
});

} // namespace
