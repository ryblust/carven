module carven:test.internal.backend.generation.formatting;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

struct FormatQuery final {
    struct Entry final {
        TargetSymbol symbol;
        std::size_t argument_count;
    };

    std::vector<Entry> entries;

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
        entries.push_back({.symbol = name->symbol, .argument_count = call->arguments.size()});
    }
    return true;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: only proved formatting selects UTF-8 storage adoption including residual and discarded results",
        [] static noexcept {
            struct Scenario final {
                std::string_view expression;
                std::optional<TargetSymbol> entry;
                std::size_t arguments;
            };

            const auto scenarios = std::to_array<Scenario>({
                {.expression = R"(f"{number:04x}/{text}")",
                 .entry = std::nullopt,
                 .arguments = 0uz},
                {.expression = R"(f"{42:04x}/{text}")", .entry = std::nullopt, .arguments = 0uz},
                {.expression = R"(f"{number:c}")",
                 .entry = TargetSymbol::RuntimeFormat,
                 .arguments = 2uz},
                {.expression = R"(f"{42}/{number:L}")",
                 .entry = TargetSymbol::RuntimeFormat,
                 .arguments = 2uz},
                {.expression = R"(f"{42}/{::probe::value()}")",
                 .entry = TargetSymbol::RuntimeFormat,
                 .arguments = 3uz},
            });
            ct::each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string_view {
                    return scenario.expression;
                },
                [](const Scenario& scenario) static noexcept {
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
                        if (!(ct::expect(traverse_target_unit(unit.sections(), query))
                                  .note("scenario.expression: ", scenario.expression))) {
                            return;
                        }
                    }
                    if (!scenario.entry) {
                        ct::expect(query.entries.empty())
                            .note("scenario.expression: ", scenario.expression);
                        return;
                    }
                    if (!(ct::expect(query.entries.size() == 1uz)
                              .note("scenario.expression: ", scenario.expression))) {
                        return;
                    }
                    ct::expect(query.entries.front().symbol == *scenario.entry)
                        .note("scenario.expression: ", scenario.expression);
                    ct::expect(query.entries.front().argument_count == scenario.arguments)
                        .note("scenario.expression: ", scenario.expression);
                }
            );
        }
    );
});

} // namespace
