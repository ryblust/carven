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

struct AppendQuery final {
    struct Entry final {
        TargetSymbol symbol;
        std::size_t argument_count;
    };

    std::vector<Entry> entries;
    std::vector<std::string> events;
    std::size_t owning_formats;
    std::size_t boolean_writes;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto AppendQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
    if (call == nullptr) {
        return true;
    }
    if (const auto* name = std::get_if<TargetIntrinsicNameExpr>(
            &template_primary_expression(*call->callee).value
        )) {
        if (name->symbol == TargetSymbol::RuntimeAppendFormat
            || name->symbol == TargetSymbol::RuntimeAppendFormatValidUTF8) {
            entries.push_back({.symbol = name->symbol, .argument_count = call->arguments.size()});
            events.push_back("format_append");
            if (!expect(call->arguments.size() >= 2uz)) {
                return false;
            }
            // The writable destination precedes the normalized format and holes.
            expect(!(std::holds_alternative<TargetStaticCastExpr>(call->arguments[0].value)));
            expect(std::holds_alternative<TargetLiteralExpr>(call->arguments[1].value));
        }
        owning_formats += name->symbol == TargetSymbol::RuntimeFormat
            || name->symbol == TargetSymbol::RuntimeFormatValidUTF8;
    }
    if (const auto* member =
            std::get_if<TargetMemberExpr>(&template_primary_expression(*call->callee).value)) {
        if (const auto* name = std::get_if<TargetIdentifier>(&member->name)) {
            boolean_writes += name->spelling() == "boolean";
        }
        if (const auto* name = std::get_if<TargetIdentifier>(&member->name);
            name != nullptr && name->spelling() == "append") {
            if (!expect(call->arguments.size() == 1uz)) {
                return false;
            }
            events.push_back(
                std::holds_alternative<TargetLiteralExpr>(call->arguments.front().value)
                    ? "append"
                    : "append_value"
            );
        }
    }
    if (const auto* name =
            std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value)) {
        const auto spelling = name->name.components().back().spelling();
        if (spelling == "select" || spelling == "touch") {
            events.emplace_back(spelling);
        }
    }
    return true;
}

auto inspect_append(std::string source) noexcept -> AppendQuery {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("formatted_append")}
    );
    auto query =
        AppendQuery {.entries = {}, .events = {}, .owning_formats = 0uz, .boolean_writes = 0uz};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        require(traverse_target_unit(unit.sections(), query));
    }
    return query;
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

    "Generation: formatted append selects its entry and passes only residual hole values"_test =
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
                 .entry = TargetSymbol::RuntimeAppendFormat,
                 .arguments = 3uz},
                {.expression = R"(f"{42}/{number:L}")",
                 .entry = TargetSymbol::RuntimeAppendFormat,
                 .arguments = 3uz},
                {.expression = R"(f"{42:04x}/{number:0{width}}/{text}")",
                 .entry = std::nullopt,
                 .arguments = 0uz},
                {.expression = R"(f"{42}/{::probe::value()}")",
                 .entry = TargetSymbol::RuntimeAppendFormat,
                 .arguments = 4uz},
            });
            each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string_view {
                    return scenario.expression;
                },
                [](const Scenario& scenario) static noexcept {
                    const auto query = inspect_append(
                        std::format(
                            "import \"probe.hpp\"; "
                            "fn append(&output: String, number: i32, width: i32, text: str) {{ "
                            "output.append_format({}); }}",
                            scenario.expression
                        )
                    );
                    if (!scenario.entry) {
                        expect(query.entries.empty())
                            .note("scenario.expression: ", scenario.expression);
                        expect(query.owning_formats == 0uz)
                            .note("scenario.expression: ", scenario.expression);
                        expect(
                            std::ranges::find(query.events, "append_value") != query.events.end()
                        )
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
                    expect(query.owning_formats == 0uz)
                        .note("scenario.expression: ", scenario.expression);
                }
            );
        };

    "Generation: runtime formatted append retains destination selection and hole effects"_test =
        [] static noexcept {
            const auto query = inspect_append(R"(
        fn select(&trace: i32) -> usize { trace += 1; return 0; }
        fn touch(&trace: i32) -> bool { trace += 1; return true; }
        fn append(&outputs: [String; 2], &trace: i32) {
            outputs[select(&trace)].append_format(f"{touch(&trace) && false}");
        }
    )");
            expect(query.entries.empty());
            expect(query.owning_formats == 0uz);
            expect_equal(query.boolean_writes, 1uz);
            expect(query.events == std::vector<std::string> {"select", "touch"});
        };

    "Generation: empty and fully known formatted append use static text directly"_test =
        [] static noexcept {
            const auto scenarios =
                std::to_array<std::string_view>({R"(f"")", R"(f"{42:04x}/{true}")"});
            each(
                scenarios,
                [](std::string_view expression) static noexcept -> std::string_view {
                    return expression;
                },
                [](std::string_view expression) static noexcept {
                    const auto query = inspect_append(
                        std::format(
                            "fn append(&output: String) {{ output.append_format({}); }}",
                            expression
                        )
                    );
                    expect(query.entries.empty()).note("expression: ", expression);
                    expect(query.owning_formats == 0uz).note("expression: ", expression);
                    expect(query.events == std::vector<std::string> {"append"})
                        .note("expression: ", expression);
                }
            );
        };
});

} // namespace
