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

namespace ct = carven::testing;

struct DisplayQuery final {
    std::size_t nodes;
    std::size_t branches;
    std::size_t pair_fields;
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
    branches += std::holds_alternative<TargetIfStmt>(statement.value);
    return true;
}

auto inspect(std::string source) noexcept -> DisplayQuery {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("display")}
    );
    auto query = DisplayQuery {.nodes = 0uz, .branches = 0uz, .pair_fields = 0uz};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        ct::require(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Generation: shared display types have bounded target syntax", [] static noexcept {
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
            ct::expect_less(query.nodes, 100uz * (depth + 1uz) * width).note("depth: ", depth);
        }
    });

    ct::test(
        "Generation: known short circuit test operands need no selection branch",
        [] static noexcept {
            const auto direct = inspect("fn verify(value: bool) { check(value); }");
            const auto skipped = inspect("fn verify(value: bool) { check(false && value); }");
            const auto selected = inspect("fn verify(value: bool) { check(true && value); }");
            ct::expect(skipped.branches == 0uz);
            ct::expect(selected.branches == direct.branches);
        }
    );

    ct::test(
        "Generation: repeated structural displays share module definitions",
        [] static noexcept {
            const auto prefix = std::string("struct Pair { first: i32, second: i32 } ");
            const auto single = inspect(prefix + "fn show(value: Pair) { println(value); }");
            const auto repeated = inspect(
                prefix
                + "fn first(value: Pair) { println(value); } "
                  "fn second(value: Pair) { println(value); } "
                  "fn third(value: Pair) { println(value); }"
            );
            ct::expect(single.pair_fields == 2uz);
            ct::expect(repeated.pair_fields == single.pair_fields);
        }
    );
});

} // namespace
