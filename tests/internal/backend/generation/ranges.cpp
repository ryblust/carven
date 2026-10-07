module carven:test.internal.backend.generation.ranges;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.stmt;
import :backend.target.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct IterationControl final {
    std::size_t structured;
    std::size_t jumps;

    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto IterationControl::enter_statement(const TargetStmt& statement) noexcept -> bool {
    structured += std::holds_alternative<TargetForStmt>(statement.value)
        || std::holds_alternative<TargetRangeForStmt>(statement.value)
        || std::holds_alternative<TargetWhileStmt>(statement.value);
    jumps += std::holds_alternative<TargetGotoStmt>(statement.value)
        || std::holds_alternative<TargetLabelStmt>(statement.value);
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: exclusive integer traversal uses structured native control"_test =
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
            auto loops = IterationControl {.structured = 0uz, .jumps = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), loops));
            }
            expect_greater(loops.structured, 0uz);
            expect_equal(loops.jumps, 0uz);
        };

    "Generation: inclusive and value traversal uses structured native control"_test =
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
            auto loops = IterationControl {.structured = 0uz, .jumps = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), loops));
            }
            expect_greater(loops.structured, 0uz);
            expect_equal(loops.jumps, 0uz);
        };
});

} // namespace
