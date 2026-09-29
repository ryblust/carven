module carven:test.internal.backend.preparation.format_encoding;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.traversal;
import :support.quote;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.format.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Format preparation: encoding proof uses types and static specifications without a value budget",
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
            ct::each(
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
                                            ct::expect(selected_plan != nullptr)
                                                .note("scenario.expression: ", scenario.expression)
                                        )) {
                                        return;
                                    }
                                    const auto& preparation =
                                        std::get<PreparedFormat>(*selected_plan);
                                    ++formats;
                                    ct::expect(
                                        (!std::holds_alternative<PreparedDelegatedFormat>(
                                             preparation
                                         )
                                         || std::get<PreparedDelegatedFormat>(preparation).encoding
                                             == FormatResultEncoding::ValidUTF8)
                                        == scenario.proven
                                    )
                                        .note("scenario.expression: ", scenario.expression);
                                    ct::expect(!(expression.constant.has_value()))
                                        .note("scenario.expression: ", scenario.expression);
                                }
                            }
                        );
                    }
                    ct::expect(formats == 1uz).note("scenario.expression: ", scenario.expression);
                }
            );
        }
    );

    ct::test(
        "Format preparation: encoding proof rejects malformed normalized formats and unknown boundaries",
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
            ct::each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string {
                    return quote_text(serialize_format(scenario.format));
                },
                [](const Scenario& scenario) static noexcept {
                    ct::expect(
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
        }
    );
});

} // namespace
