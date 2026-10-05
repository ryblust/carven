module carven:test.internal.backend.generation.ranges;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct IntegerLoops final {
    std::size_t direct;
    std::size_t ranges;
    std::size_t manual;
    std::size_t continues;
    std::size_t jumps;

    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto IntegerLoops::enter_statement(const TargetStmt& statement) noexcept -> bool {
    if (const auto* loop = std::get_if<TargetForStmt>(&statement.value)) {
        ++direct;
        if (!expect(loop->condition.has_value()) || !expect_equal(loop->steps.size(), 1uz)) {
            return false;
        }
        const auto* condition = std::get_if<TargetBinaryExpr>(&loop->condition->value);
        if (!expect(condition != nullptr)) {
            return false;
        }
        expect(condition->op == TargetBinaryOperator::Less);
        const auto* step = std::get_if<TargetUpdateStmt>(&loop->steps.front().value);
        if (!expect(step != nullptr)) {
            return false;
        }
        expect(step->op == TargetUpdateOperator::Increment);
    }
    ranges += std::holds_alternative<TargetRangeForStmt>(statement.value);
    manual += std::holds_alternative<TargetWhileStmt>(statement.value);
    continues += std::holds_alternative<TargetContinueStmt>(statement.value);
    jumps += std::holds_alternative<TargetGotoStmt>(statement.value)
        || std::holds_alternative<TargetLabelStmt>(statement.value);
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: exclusive integer ranges expose a direct cursor with a dynamic end"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                    fn sum(end: usize) -> usize {
                        var total = 0usize;
                        for index in 0..end {
                            if index == 1 { continue; }
                            total += index;
                        }
                        return total;
                    }
                )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("integer_range_cursor")}
            );
            auto loops = IntegerLoops {
                .direct = 0uz,
                .ranges = 0uz,
                .manual = 0uz,
                .continues = 0uz,
                .jumps = 0uz
            };
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), loops));
            }
            expect_equal(loops.direct, 1uz);
            expect_equal(loops.ranges, 0uz);
            expect_equal(loops.manual, 0uz);
            expect_equal(loops.continues, 1uz);
            expect_equal(loops.jumps, 0uz);
        };

    "Generation: inclusive and value ranges preserve native iteration and continue"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                    fn count(values: range<u64>, end: u64) -> usize {
                        var total = 0usize;
                        for value in 0u64..=end {
                            total += 1usize;
                            continue;
                        }
                        for value in values {
                            total += 1usize;
                            continue;
                        }
                        for value in 1u64..=1u64 {
                            total += 1usize;
                            continue;
                        }
                        return total;
                    }
                )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("integer_range_iteration")}
            );
            auto loops = IntegerLoops {
                .direct = 0uz,
                .ranges = 0uz,
                .manual = 0uz,
                .continues = 0uz,
                .jumps = 0uz
            };
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), loops));
            }
            expect_equal(loops.direct, 0uz);
            expect_equal(loops.ranges, 3uz);
            expect_equal(loops.manual, 0uz);
            expect_equal(loops.continues, 3uz);
            expect_equal(loops.jumps, 0uz);
        };
});

} // namespace
