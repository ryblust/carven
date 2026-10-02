module carven:test.internal.backend.generation.patterns;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

struct PatternQuery final {
    std::size_t comparisons;
    std::size_t unreachable;
    std::size_t bound_calls;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto PatternQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* binary = std::get_if<TargetBinaryExpr>(&expression.value)) {
        comparisons += binary->op == TargetBinaryOperator::Equal
            || binary->op == TargetBinaryOperator::GreaterEqual
            || binary->op == TargetBinaryOperator::Less
            || binary->op == TargetBinaryOperator::LessEqual;
    }
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
            bound_calls += name->name.components().back().spelling() == "bound";
        }
    }
    return true;
}

auto PatternQuery::enter_statement(const TargetStmt& statement) noexcept -> bool {
    unreachable += std::holds_alternative<TargetUnreachableStmt>(statement.value);
    return true;
}

const ct::Suite tests([] static noexcept {
    ct::test("Generation: matches omit proven residual comparisons", [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            std::size_t comparisons;
        };
        const auto cases = std::array {
            Case {
                .name = "boolean complement",
                .source = "fn select(value: bool) -> i32 => match value { "
                          "true => 1, false => 2, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "numeric enum complement",
                .source = "enum Choice { First, Second, } "
                          "fn select(value: Choice) -> i32 => match value { "
                          ".First => 1, .Second => 2, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "integer interval complement",
                .source = "fn select(value: i32) -> i32 => match value { "
                          "..0 => 1, 0.. => 2, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "guarded current pattern",
                .source = "fn select(value: bool, accept: bool) -> i32 => match value { "
                          "true => 1, false if accept => 2, false => 3, };",
                .comparisons = 1uz,
            },
            Case {
                .name = "guarded prefix retains the complement test",
                .source = "fn select(value: bool, accept: bool) -> i32 => match value { "
                          "true if accept => 1, false => 2, true => 3, };",
                .comparisons = 2uz,
            },
            Case {
                .name = "pure accepting alternatives need no selection",
                .source = "fn select(value: bool) -> i32 => match value { "
                          "true | false => 1, };",
                .comparisons = 0uz,
            },
        };
        ct::each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(std::string(input.source)),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("pattern_selection")}
            );
            auto query = PatternQuery {.comparisons = 0uz, .unreachable = 0uz, .bound_calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect_equal(query.comparisons, input.comparisons);
            ct::expect_equal(query.unreachable, 0uz);
        });
    });

    ct::test(
        "Generation: exhaustive selection retains dynamic bound evaluation",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                fn bound(&calls: i32) -> i32 { calls += 1; return 0; }
                fn select(value: i32, &calls: i32) -> i32 => match value {
                    (bound(&calls))..10 => 1,
                    _ => 2,
                };
            )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("pattern_bound")}
            );
            auto query = PatternQuery {.comparisons = 0uz, .unreachable = 0uz, .bound_calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
            }
            ct::expect_equal(query.bound_calls, 1uz);
            ct::expect_equal(query.unreachable, 0uz);
        }
    );
});

} // namespace
