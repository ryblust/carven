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
