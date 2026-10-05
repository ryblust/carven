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
    std::size_t indirect;

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
    indirect += std::holds_alternative<TargetRangeForStmt>(statement.value)
        || std::holds_alternative<TargetWhileStmt>(statement.value);
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
            auto loops = IntegerLoops {.direct = 0uz, .indirect = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), loops));
            }
            expect_equal(loops.direct, 1uz);
            expect_equal(loops.indirect, 0uz);
        };
});

} // namespace
