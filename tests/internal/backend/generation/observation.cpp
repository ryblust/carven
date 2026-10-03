module carven:test.internal.backend.generation.observation;

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

struct ObservationQuery final {
    std::size_t locals;
    std::size_t pure_calls;

    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole role) noexcept -> bool;
};

auto ObservationQuery::visit_variable(const TargetVariableStmt&) noexcept -> bool {
    ++locals;
    return true;
}

auto ObservationQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* name =
                std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value)) {
            pure_calls += name->name.components().back().spelling() == "pure";
        }
    }
    return true;
}

const ct::Suite tests([] static noexcept {
    ct::test("Generation: unexposed snapshot owners need no operand storage", [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            std::size_t locals;
            std::size_t pure_calls;
        };
        const auto cases = std::array {
            Case {
                .name = "immutable local before call",
                .source = "fn probe(input: i32) -> i32 { let value = input; "
                          "return pair(value, pure(3)); }",
                .locals = 1uz,
                .pure_calls = 1uz
            },
            Case {
                .name = "unmodified mutable local before call",
                .source = "fn probe(input: i32) -> i32 { var value = input; "
                          "return pair(value, pure(3)); }",
                .locals = 1uz,
                .pure_calls = 1uz
            },
            Case {
                .name = "Take parameter before call",
                .source = "fn probe(&&value: i32) -> i32 => pair(value, pure(3));",
                .locals = 0uz,
                .pure_calls = 1uz
            },
            Case {
                .name = "disjoint local write",
                .source = "fn bump(&target: i32) -> i32 { target += 1; return target; } "
                          "fn probe(input: i32) -> i32 { let value = input; var other = 2; "
                          "return pair(value, bump(&other)); }",
                .locals = 2uz,
                .pure_calls = 0uz
            },
            Case {
                .name = "checked division",
                .source = "fn probe(input: i32, divisor: i32) -> i32 { let value = input; "
                          "return pair(value, 100 / divisor); }",
                .locals = 1uz,
                .pure_calls = 0uz
            },
            Case {
                .name = "Write access retains an earlier snapshot",
                .source = "fn bump(&target: i32) -> i32 { target += 1; return target; } "
                          "fn probe(input: i32) -> i32 { var value = input; "
                          "return pair(value, bump(&value)); }",
                .locals = 2uz,
                .pure_calls = 0uz
            },
        };
        ct::each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn pure(input: i32) -> i32 => input; "
                    "fn pair(first: i32, second: i32) -> i32 => first + second; "
                    + std::string(input.source)
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("observation")}
            );
            auto query = ObservationQuery {.locals = 0uz, .pure_calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                ct::expect(traverse_target_unit(unit.sections(), query));
            }
            ct::expect_equal(query.locals, input.locals);
            ct::expect_equal(query.pure_calls, input.pure_calls);
        });
    });
});

} // namespace
