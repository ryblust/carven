module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.display;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct DisplayQuery final {
    std::size_t nodes;
    std::size_t branches;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto DisplayQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    static_cast<void>(expression);
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
    auto query = DisplayQuery {.nodes = 0uz, .branches = 0uz};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        REQUIRE(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

} // namespace

TEST_CASE("Generation: shared display types have bounded target syntax") {
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
        CAPTURE(depth);
        CHECK_LT(query.nodes, 100uz * (depth + 1uz) * width);
    }
}

TEST_CASE("Generation: known short circuit test operands need no selection branch") {
    const auto direct = inspect("fn verify(value: bool) { check(value); }");
    const auto skipped = inspect("fn verify(value: bool) { check(false && value); }");
    const auto selected = inspect("fn verify(value: bool) { check(true && value); }");
    CHECK(skipped.branches == direct.branches);
    CHECK(selected.branches == direct.branches);
}
