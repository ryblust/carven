module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.reporting;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct ReportQuery final {
    const TargetUnit& unit;
    std::size_t branches;
    std::size_t writers;
    std::size_t assertions;
    std::size_t calls;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
};

auto ReportQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(&call->callee->value)) {
            assertions += intrinsic->symbol == TargetSymbol::RuntimeAssertionFailed;
        }
        calls += std::holds_alternative<TargetNameExpr>(call->callee->value);
    }
    return true;
}

auto ReportQuery::enter_statement(const TargetStmt& statement) noexcept -> bool {
    branches += std::holds_alternative<TargetIfStmt>(statement.value);
    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
        if (const auto* intrinsic =
                std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value)) {
            writers += intrinsic->symbol == TargetSymbol::RuntimeDisplayWriter;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Generation: known reports preserve effects and omit unnecessary report work") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(R"(
            fn touch() -> bool { return false; }
            fn successful() { assert(touch() || true, "skipped"); assert(1 == 1); }
            fn fatal() { assert(false); touch(); }
        )"),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("reporting")}
    );
    auto branches = 0uz;
    auto writers = 0uz;
    auto assertions = 0uz;
    auto calls = 0uz;
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto query = ReportQuery {
            .unit = unit,
            .branches = 0uz,
            .writers = 0uz,
            .assertions = 0uz,
            .calls = 0uz
        };
        REQUIRE(traverse_target_unit(unit.sections(), query));
        branches += query.branches;
        writers += query.writers;
        assertions += query.assertions;
        calls += query.calls;
    }
    CHECK(branches == 0uz);
    CHECK(writers == 0uz);
    CHECK(assertions == 1uz);
    CHECK(calls == 1uz);
}
