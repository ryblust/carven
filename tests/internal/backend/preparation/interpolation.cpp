module carven:test.internal.backend.preparation.interpolation;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.format;
import :semantic.semir.traversal;
import :support.quote;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.format.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Format preparation: static contents fold while runtime holes retain source operands"_test =
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
                 std::nullopt},
                {R"(const version = 42; const copy = version; return f"build-{copy:04}";)",
                 "build-0042"},
                {R"(let text = "我\0"; return f"{text}";)", std::nullopt},
                {R"(const text = "我\0"; return f"{text}";)", std::string_view("我\0", 4)},
                {R"(return f"{touch() && false}";)", std::nullopt},
                {R"(return f"{false && touch()}";)", "false"},
                {R"(var version = 42; return f"{version}";)", std::nullopt},
                {R"(return f"{1.25}";)", "1.25"},
                {R"(return f"{42:>{4}}";)", std::nullopt},
                {R"(return f"{42:+04}";)", std::nullopt},
                {R"(return f"{42:00}";)", std::nullopt},
                {R"(return f"{42:65537}";)", std::nullopt},
                {R"(return f"x{42:65536}";)", std::nullopt},
            });
            each(
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
                                if (!(expect(selected_plan != nullptr)
                                          .note("scenario.body: ", scenario.body))) {
                                    return;
                                }
                                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                                ++formats;
                                expect(!(expression.constant.has_value()))
                                    .note("scenario.body: ", scenario.body);
                                const auto* prepared =
                                    std::get_if<PreparedFormatText>(&preparation);
                                expect((prepared != nullptr) == scenario.contents.has_value())
                                    .note("scenario.body: ", scenario.body);
                                if (prepared && scenario.contents) {
                                    expect(prepared->text == *scenario.contents)
                                        .note("scenario.body: ", scenario.body);
                                }
                            }
                        );
                    }
                    expect(formats == 1uz).note("scenario.body: ", scenario.body);
                }
            );
        };

    "Format preparation: mixed builtin holes publish an ordered residual format"_test =
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
            each(
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
                                if (!(expect(selected_plan != nullptr)
                                          .note("scenario.expression: ", scenario.expression))) {
                                    return;
                                }
                                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                                ++count;
                                expect(!(expression.constant.has_value()))
                                    .note("scenario.expression: ", scenario.expression);
                                expect(!(std::holds_alternative<PreparedFormatText>(preparation)))
                                    .note("scenario.expression: ", scenario.expression);
                                expect(format->operands.size() == scenario.operands)
                                    .note("scenario.expression: ", scenario.expression);
                                const auto* delegated =
                                    std::get_if<PreparedDelegatedFormat>(&preparation);
                                const auto indices = prepared_format_operands(preparation);
                                if (!(expect(
                                          (indices.size() < format->operands.size())
                                          == scenario.remainder.has_value()
                                    )
                                          .note("scenario.expression: ", scenario.expression))) {
                                    return;
                                }
                                if (scenario.remainder) {
                                    expect_range_equal(indices, scenario.indices)
                                        .note("scenario.expression: ", scenario.expression);
                                    if (delegated != nullptr) {
                                        expect(delegated->format_string == *scenario.remainder)
                                            .note("scenario.expression: ", scenario.expression);
                                    }
                                }
                            }
                        );
                    }
                    expect(count == 1uz).note("scenario.expression: ", scenario.expression);
                }
            );
        };

    "Format preparation: delegated residual byte budgets include escaped braces"_test =
        [] static noexcept {
            for (const auto length : {32765uz, 32766uz}) {
                const auto braces = std::string(length, '{');
                const auto program = analyze_test_program(
                    std::format(
                        "fn format(value: f64) -> String {{ const text = \"{}\"; return f\"{{text}}{{value:a}}\"; }}",
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
                                if (!expect(selected_plan != nullptr)) {
                                    return;
                                }
                                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                                ++count;
                                expect(
                                    (prepared_format_operands(preparation).size()
                                     < format->operands.size())
                                    == (length == 32765uz)
                                );
                            }
                        }
                    );
                }
                expect(count == 1uz);
            }
        };

    "Format preparation: delegated format strings preserve opaque braces"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn format(value: i32) -> String => f"{42}/{value:\u{7b}}";
    )");
            auto checked = false;
            for (const auto callable : test_function_callables(program)) {
                const auto body = program.declarations().body_for_callable(callable);
                if (!expect(body.has_value())) {
                    return;
                }
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (std::holds_alternative<SemFormat>(expression.value)) {
                            const auto selected_plan = prepare_operation(program, expression);
                            if (!expect(selected_plan != nullptr)) {
                                return;
                            }
                            const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                            const auto* delegated =
                                std::get_if<PreparedDelegatedFormat>(&preparation);
                            if (!expect(delegated != nullptr)) {
                                return;
                            }
                            expect(delegated->format_string == "42/{0:{}");
                            expect(delegated->operand_indices == std::vector<std::size_t> {1});
                            expect(delegated->encoding == FormatResultEncoding::Unproven);
                            checked = true;
                        }
                    }
                );
            }
            expect(checked);
        };

    "Format preparation: encoding proof uses types and static specifications without a value budget"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view expression;
                bool proven;
            };

            const auto scenarios = std::to_array<Scenario>({
                {.expression = R"(f"")", .proven = true},
                {.expression = R"(f"{{我}}\0{number:04x}/{text}/{owned}/{flag}/{character}")",
                 .proven = true},
                {.expression = R"(f"{number:65537}")", .proven = true},
                {.expression = R"(f"{7:65537}/{number}")", .proven = true},
                {.expression = R"(f"{7}/{number}")", .proven = true},
                {.expression = R"(f"{7}/{::probe::value() as i32}")", .proven = true},
                {.expression = R"(f"{7}/{::probe::value()}")", .proven = false},
                {.expression = R"(f"{number:c}")", .proven = false},
                {.expression = R"(f"{number:L}")", .proven = false},
                {.expression = R"(f"{number:00}")", .proven = false},
                {.expression = R"(f"{number:我>8}")", .proven = false},
                {.expression = R"(f"{number:0{4}}")", .proven = true},
                {.expression = R"(f"{text:.2}")", .proven = false},
                {.expression = R"(f"{character:4}")", .proven = false},
                {.expression = R"(f"{flag:d}")", .proven = false},
                {.expression = R"(f"{real}")", .proven = true},
            });
            each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string_view {
                    return scenario.expression;
                },
                [](const Scenario& scenario) static noexcept {
                    const auto program = analyze_test_program(
                        std::format(
                            "import \"probe.hpp\"; fn format(number: i32, text: str, owned: String, "
                            "flag: bool, character: char, real: f64) -> String => {};",
                            scenario.expression
                        )
                    );
                    auto formats = 0uz;
                    for (const auto entry : program.bodies().entries()) {
                        visit_semantic_nodes(
                            entry.value.region(),
                            [&](const SemanticExpression& expression) noexcept {
                                if (std::holds_alternative<SemFormat>(expression.value)) {
                                    const auto selected_plan =
                                        prepare_operation(program, expression);
                                    if (!(
                                            expect(selected_plan != nullptr)
                                                .note("scenario.expression: ", scenario.expression)
                                        )) {
                                        return;
                                    }
                                    const auto& preparation =
                                        std::get<PreparedFormat>(*selected_plan);
                                    ++formats;
                                    expect(
                                        (!std::holds_alternative<PreparedDelegatedFormat>(
                                             preparation
                                         )
                                         || std::get<PreparedDelegatedFormat>(preparation).encoding
                                             == FormatResultEncoding::ValidUTF8)
                                        == scenario.proven
                                    )
                                        .note("scenario.expression: ", scenario.expression);
                                    expect(!(expression.constant.has_value()))
                                        .note("scenario.expression: ", scenario.expression);
                                }
                            }
                        );
                    }
                    expect(formats == 1uz).note("scenario.expression: ", scenario.expression);
                }
            );
        };

    "Format preparation: encoding proof rejects malformed normalized formats and unknown boundaries"_test =
        [] static noexcept {
            struct Scenario final {
                FormatSpec format;
                std::vector<std::optional<BuiltinType>> types;
                bool proven;
            };

            const auto scenarios = std::to_array<Scenario>({
                {.format = FormatSpec {.parts = {format_field(0uz, {format_text("04x")})}},
                 .types = {BuiltinType::I32},
                 .proven = true},
                {.format =
                     FormatSpec {
                         .parts =
                             {format_field(0uz, {format_text("999999999999999999999999999999")})}
                     },
                 .types = {BuiltinType::I32},
                 .proven = true},
                {.format =
                     FormatSpec {
                         .parts = {format_text(std::string("我\0", 4uz)), format_field(0uz)}
                     },
                 .types = {BuiltinType::Str},
                 .proven = true},
                {.format = FormatSpec {.parts = {format_field(0uz)}},
                 .types = {std::nullopt},
                 .proven = false},
                {.format = FormatSpec {.parts = {format_field(0uz)}},
                 .types = {BuiltinType::Void},
                 .proven = false},
                {.format = FormatSpec {.parts = {format_field(0uz)}}, .types = {}, .proven = false},
                {.format = FormatSpec {.parts = {format_field(0uz), format_field(0uz)}},
                 .types = {BuiltinType::I32},
                 .proven = false},
                {.format = FormatSpec {.parts = {format_field(1uz), format_field(0uz)}},
                 .types = {BuiltinType::I32, BuiltinType::I32},
                 .proven = false},
                {.format =
                     FormatSpec {
                         .parts = {format_field(0uz, {format_text("0"), format_field(1uz)})}
                     },
                 .types = {BuiltinType::I32, BuiltinType::I32},
                 .proven = false},
                {.format = FormatSpec {.parts = {format_field(0uz)}},
                 .types = {BuiltinType::I32, BuiltinType::I32},
                 .proven = false},
                {.format = FormatSpec {.parts = {format_text("\xff"), format_field(0uz)}},
                 .types = {BuiltinType::I32},
                 .proven = false},
            });
            each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string {
                    return quote_text(serialize_format(scenario.format));
                },
                [](const Scenario& scenario) static noexcept {
                    expect(
                        format_preserves_utf8(scenario.format, scenario.types) == scenario.proven
                    )
                        .note([&] noexcept {
                            return std::format(
                                "serialize_format(scenario.format): {}",
                                serialize_format(scenario.format)
                            );
                        });
                }
            );
        };
});

} // namespace
