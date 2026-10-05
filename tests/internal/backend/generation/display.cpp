module carven:test.internal.backend.generation.display;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct DisplayQuery final {
    std::size_t nodes;
    std::size_t branches;
    std::size_t pair_fields;
    std::size_t enum_conditions;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto DisplayQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* member = std::get_if<TargetMemberExpr>(&expression.value)) {
        if (const auto* name = std::get_if<TargetIdentifier>(&member->name)) {
            pair_fields += name->spelling() == "first" || name->spelling() == "second";
        }
    }
    ++nodes;
    return true;
}

auto DisplayQuery::enter_statement(const TargetStmt& statement) noexcept -> bool {
    if (const auto* selection = std::get_if<TargetIfStmt>(&statement.value)) {
        ++branches;
        for (const auto& branch : selection->branches) {
            if (const auto* condition = std::get_if<TargetBinaryExpr>(&branch.condition.value)) {
                enum_conditions += condition->op == TargetBinaryOperator::Equal
                    || condition->op == TargetBinaryOperator::NotEqual;
            }
        }
    }
    return true;
}

auto inspect(std::string source) noexcept -> DisplayQuery {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("display")}
    );
    auto query = DisplayQuery {
        .nodes = 0uz,
        .branches = 0uz,
        .pair_fields = 0uz,
        .enum_conditions = 0uz,
    };
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        require(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

const TestSuite suite([] static noexcept {
    "Generation: enum display selects only unresolved cases"_test = [] static noexcept {
        struct Scenario final {
            std::string_view name;
            std::string_view source;
            std::size_t conditions;
        };
        const auto scenarios = std::array {
            Scenario {
                "single numeric case",
                "enum E { A } fn show(value: E) { println(value); }",
                0uz,
            },
            Scenario {
                "single payload case",
                "enum E { A(i32) } fn show(value: E) { println(value); }",
                0uz,
            },
            Scenario {
                "numeric cases",
                "enum E { A, B, C } fn show(value: E) { println(value); }",
                2uz,
            },
            Scenario {
                "payload cases",
                "enum E { A(i32), B } fn show(value: E) { println(value); }",
                1uz,
            },
        };
        each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
            const auto query = inspect(std::string(scenario.source));
            expect_equal(query.enum_conditions, scenario.conditions);
        });
    };

    "Generation: shared display types have bounded target syntax"_test = [] static noexcept {
        constexpr auto width = 4uz;
        const auto depths = std::array {2uz, 4uz, 6uz};
        for (const auto depth : depths) {
            auto source = std::string("struct N0 { value: i32 }\n");
            for (auto level = 1uz; level <= depth; ++level) {
                source += std::format("struct N{} {{ ", level);
                for (auto field = 0uz; field < width; ++field) {
                    source += std::format("f{}: N{}, ", field, level - 1uz);
                }
                source += "}\n";
            }
            source += std::format("fn show(value: N{}) {{ println(value); }}", depth);
            const auto query = inspect(std::move(source));
            expect_less(query.nodes, 100uz * (depth + 1uz) * width).note("depth: ", depth);
        }
    };

    "Generation: known short circuit test operands need no selection branch"_test =
        [] static noexcept {
            const auto direct = inspect("fn verify(value: bool) { check(value); }");
            const auto skipped = inspect("fn verify(value: bool) { check(false && value); }");
            const auto selected = inspect("fn verify(value: bool) { check(true && value); }");
            expect(skipped.branches == 0uz);
            expect(selected.branches == direct.branches);
        };

    "Generation: repeated structural displays share module definitions"_test = [] static noexcept {
        const auto prefix = std::string("struct Pair { first: i32, second: i32 } ");
        const auto single = inspect(prefix + "fn show(value: Pair) { println(value); }");
        const auto repeated = inspect(
            prefix
            + "fn first(value: Pair) { println(value); } "
              "fn second(value: Pair) { println(value); } "
              "fn third(value: Pair) { println(value); }"
        );
        expect(single.pair_fields == 2uz);
        expect(repeated.pair_fields == single.pair_fields);
    };
});

} // namespace
