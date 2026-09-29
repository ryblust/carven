module carven:test.internal.backend.preparation.interpolation;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.format;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Format preparation: known contents retain owning operations and source operands",
        [] static noexcept {
            struct Scenario final {
                std::string_view body;
                std::optional<std::string_view> contents;
            };

            const auto scenarios = std::to_array<Scenario>({
                {R"(return f"";)", ""},
                {R"(return f"{{x}}\0我";)", std::string_view("{x}\0我", 7)},
                {R"(return f"{42:04} {-42:06x} {true} {'😀'} {"{text}"}";)",
                 "0042 -0002a true 😀 {text}"},
                {R"(let version = 42; let copy = version; return f"build-{copy:04}";)",
                 "build-0042"},
                {R"(let text = "我\0"; return f"{text}";)", std::string_view("我\0", 4)},
                {R"(return f"{touch() && false}";)", "false"},
                {R"(var version = 42; return f"{version}";)", std::nullopt},
                {R"(return f"{1.25}";)", "1.25"},
                {R"(return f"{42:>{4}}";)", std::nullopt},
                {R"(return f"{42:+04}";)", std::nullopt},
                {R"(return f"{42:00}";)", std::nullopt},
                {R"(return f"{42:65537}";)", std::nullopt},
                {R"(return f"x{42:65536}";)", std::nullopt},
            });
            ct::each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string_view {
                    return scenario.body;
                },
                [](const Scenario& scenario) static noexcept {
                    const auto program = analyze_test_program(
                        std::format(
                            "fn touch() -> bool {{ return true; }} fn format() -> String {{ {} }}",
                            scenario.body
                        )
                    );
                    auto formats = 0uz;
                    for (const auto entry : program.bodies().entries()) {
                        visit_semantic_nodes(
                            entry.value.region(),
                            [&](const SemanticExpression& expression) noexcept {
                                const auto* format = std::get_if<SemFormat>(&expression.value);
                                if (format == nullptr) {
                                    return;
                                }
                                const auto selected_plan = prepare_operation(program, expression);
                                if (!(ct::expect(selected_plan != nullptr)
                                          .note("scenario.body: ", scenario.body))) {
                                    return;
                                }
                                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                                ++formats;
                                ct::expect(!(expression.constant.has_value()))
                                    .note("scenario.body: ", scenario.body);
                                const auto* prepared =
                                    std::get_if<PreparedFormatText>(&preparation);
                                ct::expect((prepared != nullptr) == scenario.contents.has_value())
                                    .note("scenario.body: ", scenario.body);
                                if (prepared && scenario.contents) {
                                    ct::expect(prepared->text == *scenario.contents)
                                        .note("scenario.body: ", scenario.body);
                                }
                            }
                        );
                    }
                    ct::expect(formats == 1uz).note("scenario.body: ", scenario.body);
                }
            );
        }
    );

    ct::test(
        "Format preparation: mixed builtin holes publish an ordered residual format",
        [] static noexcept {
            struct Scenario final {
                std::string_view expression;
                std::optional<std::string_view> remainder;
                std::vector<std::size_t> indices;
                std::size_t operands;
            };

            const auto scenarios = std::to_array<Scenario>({
                {R"(f"build-{42:04}: {value}")", "build-0042: {0}", {1uz}, 2uz},
                {R"(f"{value}|{7:x}|{name}|{false}")", "{0}|7|{1}|false", {0uz, 2uz}, 4uz},
                {R"(f"{"{x}"}\0我{value}")", std::string_view("{{x}}\0我{0}", 12uz), {1uz}, 2uz},
                {R"(f"{42}/{value:{width}.{precision}f}/{7}")",
                 "42/{0:{1}.{2}f}/7",
                 {1uz, 2uz, 3uz},
                 5uz},
                {R"(f"{7}:{42:0{width}}")", "7:{0:0{1}}", {1uz, 2uz}, 3uz},
                {R"(f"{7}:{42:00}/{value}")", "7:{0:00}/{1}", {1uz, 2uz}, 3uz},
                {R"(f"{value}")", std::nullopt, {}, 1uz},
                {R"(f"{42}{::probe::value()}")", std::nullopt, {}, 2uz},
                {R"(f"{42}{1:65537}/{value}")", "42{0:65537}/{1}", {1uz, 2uz}, 3uz},
                {R"(f"{1:65536}/{value}")", std::nullopt, {}, 2uz},
            });
            ct::each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string_view {
                    return scenario.expression;
                },
                [](const Scenario& scenario) static noexcept {
                    const auto program = analyze_test_program(
                        std::format(
                            "import \"provider.hpp\"; "
                            "fn format(value: f64, name: str, width: i32, precision: i32) -> String {{ return {}; }}",
                            scenario.expression
                        )
                    );
                    auto count = 0uz;
                    for (const auto entry : program.bodies().entries()) {
                        visit_semantic_nodes(
                            entry.value.region(),
                            [&](const SemanticExpression& expression) noexcept {
                                const auto* format = std::get_if<SemFormat>(&expression.value);
                                if (format == nullptr) {
                                    return;
                                }
                                const auto selected_plan = prepare_operation(program, expression);
                                if (!(ct::expect(selected_plan != nullptr)
                                          .note("scenario.expression: ", scenario.expression))) {
                                    return;
                                }
                                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                                ++count;
                                ct::expect(!(expression.constant.has_value()))
                                    .note("scenario.expression: ", scenario.expression);
                                ct::expect(
                                    !(std::holds_alternative<PreparedFormatText>(preparation))
                                )
                                    .note("scenario.expression: ", scenario.expression);
                                ct::expect(format->operands.size() == scenario.operands)
                                    .note("scenario.expression: ", scenario.expression);
                                const auto* delegated =
                                    std::get_if<PreparedDelegatedFormat>(&preparation);
                                const auto indices = prepared_format_operands(preparation);
                                if (!(ct::expect(
                                          (indices.size() < format->operands.size())
                                          == scenario.remainder.has_value()
                                    )
                                          .note("scenario.expression: ", scenario.expression))) {
                                    return;
                                }
                                if (scenario.remainder) {
                                    ct::expect_range_equal(indices, scenario.indices)
                                        .note("scenario.expression: ", scenario.expression);
                                    if (delegated != nullptr) {
                                        ct::expect(delegated->format_string == *scenario.remainder)
                                            .note("scenario.expression: ", scenario.expression);
                                    }
                                }
                            }
                        );
                    }
                    ct::expect(count == 1uz).note("scenario.expression: ", scenario.expression);
                }
            );
        }
    );

    ct::test(
        "Format preparation: delegated residual byte budgets include escaped braces",
        [] static noexcept {
            for (const auto length : {32765uz, 32766uz}) {
                const auto braces = std::string(length, '{');
                const auto program = analyze_test_program(
                    std::format(
                        "fn format(value: f64) -> String {{ let text = \"{}\"; return f\"{{text}}{{value:a}}\"; }}",
                        braces
                    )
                );
                auto count = 0uz;
                for (const auto entry : program.bodies().entries()) {
                    visit_semantic_nodes(
                        entry.value.region(),
                        [&](const SemanticExpression& expression) noexcept {
                            if (const auto* format = std::get_if<SemFormat>(&expression.value)) {
                                const auto selected_plan = prepare_operation(program, expression);
                                if (!ct::expect(selected_plan != nullptr)) {
                                    return;
                                }
                                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                                ++count;
                                ct::expect(
                                    (prepared_format_operands(preparation).size()
                                     < format->operands.size())
                                    == (length == 32765uz)
                                );
                            }
                        }
                    );
                }
                ct::expect(count == 1uz);
            }
        }
    );

    ct::test(
        "Format preparation: delegated format strings preserve opaque braces",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn format(value: i32) -> String => f"{42}/{value:\u{7b}}";
    )");
            auto checked = false;
            for (const auto callable : test_function_callables(program)) {
                const auto body = program.declarations().body_for_callable(callable);
                if (!ct::expect(body.has_value())) {
                    return;
                }
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (std::holds_alternative<SemFormat>(expression.value)) {
                            const auto selected_plan = prepare_operation(program, expression);
                            if (!ct::expect(selected_plan != nullptr)) {
                                return;
                            }
                            const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                            const auto* delegated =
                                std::get_if<PreparedDelegatedFormat>(&preparation);
                            if (!ct::expect(delegated != nullptr)) {
                                return;
                            }
                            ct::expect(delegated->format_string == "42/{0:{}");
                            ct::expect(delegated->operand_indices == std::vector<std::size_t> {1});
                            ct::expect(delegated->encoding == FormatResultEncoding::Unproven);
                            checked = true;
                        }
                    }
                );
            }
            ct::expect(checked);
        }
    );
});

} // namespace
