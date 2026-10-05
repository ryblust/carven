module carven:test.internal.backend.generation.formatting;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

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
    const auto* name =
        std::get_if<TargetIntrinsicNameExpr>(&template_primary_expression(*call->callee).value);
    if (name != nullptr
        && (name->symbol == TargetSymbol::RuntimeFormat
            || name->symbol == TargetSymbol::RuntimeFormatValidUTF8)) {
        entries.push_back({.symbol = name->symbol, .argument_count = call->arguments.size()});
    }
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: only proved formatting selects UTF-8 storage adoption including residual and discarded results"_test =
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
            each(
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
                        if (!(expect(traverse_target_unit(unit.sections(), query))
                                  .note("scenario.expression: ", scenario.expression))) {
                            return;
                        }
                    }
                    if (!scenario.entry) {
                        expect(query.entries.empty())
                            .note("scenario.expression: ", scenario.expression);
                        return;
                    }
                    if (!(expect(query.entries.size() == 1uz)
                              .note("scenario.expression: ", scenario.expression))) {
                        return;
                    }
                    expect(query.entries.front().symbol == *scenario.entry)
                        .note("scenario.expression: ", scenario.expression);
                    expect(query.entries.front().argument_count == scenario.arguments)
                        .note("scenario.expression: ", scenario.expression);
                }
            );
        };

    "Generation: precomputed formatting retains effects"_test = [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(
                "fn touch() -> bool { return true; }\n"
                "fn known() -> String { const version = 42; return f\"build-{version:04}\"; }\n"
                "fn effects() { f\"{touch() && false}\"; }\n"
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("precomputed_format")}
        );

        struct Query final {
            std::size_t formats;
            std::size_t effects;

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                if (const auto* name = std::get_if<TargetIntrinsicNameExpr>(&expression.value)) {
                    formats += name->symbol == TargetSymbol::RuntimeFormat
                        || name->symbol == TargetSymbol::RuntimeFormatValidUTF8;
                }
                if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                    if (const auto* name = std::get_if<TargetNameExpr>(
                            &template_primary_expression(*call->callee).value
                        )) {
                        effects += name->name.components().back().spelling() == "touch";
                    }
                }
                return true;
            }
        };

        auto query = Query {.formats = 0uz, .effects = 0uz};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            expect(traverse_target_unit(unit.sections(), query));
        }
        expect_equal(query.formats, 0uz);
        expect_equal(query.effects, 1uz);
    };

    "Generation: mixed formatting passes only residual values after required effects"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "fn touch() -> bool { return true; } "
                    "fn format(value: f64, width: i32, precision: i32) -> String { "
                    "return f\"{42:04}/{touch() && false}/{value:{width}.{precision}f}/{7}\"; }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("mixed_format")}
            );

            struct Query final {
                std::size_t formats;
                std::size_t effects;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call == nullptr) {
                        return true;
                    }
                    if (const auto* name = std::get_if<TargetNameExpr>(
                            &template_primary_expression(*call->callee).value
                        )) {
                        effects += name->name.components().back().spelling() == "touch";
                    }
                    if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(
                            &template_primary_expression(*call->callee).value
                        );
                        intrinsic != nullptr && intrinsic->symbol == TargetSymbol::RuntimeFormat) {
                        ++formats;
                    }
                    return true;
                }
            };

            auto query = Query {.formats = 0uz, .effects = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect(query.formats == 1uz);
            expect(query.effects == 1uz);
        };
});

} // namespace
