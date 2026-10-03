module carven:test.internal.backend.generation.formatted_append;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

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
            if (!ct::expect(call->arguments.size() >= 2uz)) {
                return false;
            }
            // The writable destination precedes the normalized format and holes.
            ct::expect(!(std::holds_alternative<TargetStaticCastExpr>(call->arguments[0].value)));
            ct::expect(std::holds_alternative<TargetLiteralExpr>(call->arguments[1].value));
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
            if (!ct::expect(call->arguments.size() == 1uz)) {
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
        ct::require(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: formatted append selects its entry and passes only residual hole values",
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
            ct::each(
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
                        ct::expect(query.entries.empty())
                            .note("scenario.expression: ", scenario.expression);
                        ct::expect(query.owning_formats == 0uz)
                            .note("scenario.expression: ", scenario.expression);
                        ct::expect(
                            std::ranges::find(query.events, "append_value") != query.events.end()
                        )
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
                    ct::expect(query.owning_formats == 0uz)
                        .note("scenario.expression: ", scenario.expression);
                }
            );
        }
    );

    ct::test(
        "Generation: runtime formatted append retains destination selection and hole effects",
        [] static noexcept {
            const auto query = inspect_append(R"(
        fn select(&trace: i32) -> usize { trace += 1; return 0; }
        fn touch(&trace: i32) -> bool { trace += 1; return true; }
        fn append(&outputs: [String; 2], &trace: i32) {
            outputs[select(&trace)].append_format(f"{touch(&trace) && false}");
        }
    )");
            ct::expect(query.entries.empty());
            ct::expect(query.owning_formats == 0uz);
            ct::expect_equal(query.boolean_writes, 1uz);
            ct::expect(query.events == std::vector<std::string> {"select", "touch"});
        }
    );

    ct::test(
        "Generation: empty and fully known formatted append use static text directly",
        [] static noexcept {
            const auto scenarios =
                std::to_array<std::string_view>({R"(f"")", R"(f"{42:04x}/{true}")"});
            ct::each(
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
                    ct::expect(query.entries.empty()).note("expression: ", expression);
                    ct::expect(query.owning_formats == 0uz).note("expression: ", expression);
                    ct::expect(query.events == std::vector<std::string> {"append"})
                        .note("expression: ", expression);
                }
            );
        }
    );
});

} // namespace
