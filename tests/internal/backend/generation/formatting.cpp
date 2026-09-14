module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.formatting;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct FormatQuery final {
    std::vector<TargetSymbol> entries;
    std::vector<std::size_t> argument_counts;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto FormatQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
    if (call == nullptr) {
        return true;
    }
    const auto* name = std::get_if<TargetIntrinsicNameExpr>(&call->callee->value);
    if (name != nullptr
        && (name->symbol == TargetSymbol::RuntimeFormat
            || name->symbol == TargetSymbol::RuntimeFormatValidUTF8)) {
        entries.push_back(name->symbol);
        argument_counts.push_back(call->arguments.size());
    }
    return true;
}

} // namespace

TEST_CASE(
    "Generation: only proved formatting selects UTF-8 storage adoption including residual and discarded results"
) {
    struct Scenario final {
        std::string_view expression;
        TargetSymbol entry;
        std::size_t arguments;
    };

    const auto scenarios = std::to_array<Scenario>({
        {.expression = R"(f"{number:04x}/{text}")",
         .entry = TargetSymbol::RuntimeFormatValidUTF8,
         .arguments = 3uz},
        {.expression = R"(f"{42:04x}/{text}")",
         .entry = TargetSymbol::RuntimeFormatValidUTF8,
         .arguments = 2uz},
        {.expression = R"(f"{number:c}")", .entry = TargetSymbol::RuntimeFormat, .arguments = 2uz},
        {.expression = R"(f"{42}/{number:L}")",
         .entry = TargetSymbol::RuntimeFormat,
         .arguments = 2uz},
        {.expression = R"(f"{42}/{::probe::value()}")",
         .entry = TargetSymbol::RuntimeFormat,
         .arguments = 3uz},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.expression);
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(
                std::format(
                    "import \"probe.hpp\"; fn discarded(number: i32, text: str) {{ {}; }}",
                    scenario.expression
                )
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("format_encoding")}
        );
        auto query = FormatQuery();
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            REQUIRE(traverse_target_unit(unit.sections(), query));
        }
        REQUIRE(query.entries.size() == 1uz);
        CHECK(query.entries.front() == scenario.entry);
        REQUIRE(query.argument_counts.size() == 1uz);
        CHECK(query.argument_counts.front() == scenario.arguments);
    }
}
