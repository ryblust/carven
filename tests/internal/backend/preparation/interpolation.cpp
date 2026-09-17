module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.preparation.interpolation;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.format;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Format preparation: known contents retain owning operations and source operands") {
    struct Scenario final {
        std::string_view body;
        std::optional<std::string_view> contents;
    };

    const auto scenarios = std::to_array<Scenario>({
        {R"(return f"";)", ""},
        {R"(return f"{{x}}\0我";)", std::string_view("{x}\0我", 7)},
        {R"(return f"{42:04} {-42:06x} {true} {'😀'} {"{text}"}";)", "0042 -0002a true 😀 {text}"},
        {R"(let version = 42; let copy = version; return f"build-{copy:04}";)", "build-0042"},
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
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.body);
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
                    REQUIRE(selected_plan != nullptr);
                    const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                    ++formats;
                    CHECK_FALSE(expression.constant.has_value());
                    const auto* prepared = std::get_if<PreparedFormatText>(&preparation);
                    CHECK((prepared != nullptr) == scenario.contents.has_value());
                    if (prepared && scenario.contents) {
                        CHECK(prepared->text == *scenario.contents);
                    }
                }
            );
        }
        CHECK(formats == 1uz);
    }
}

TEST_CASE("Format preparation: mixed builtin holes publish an ordered residual format") {
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
        {R"(f"{42}/{value:{width}.{precision}f}/{7}")", "42/{0:{1}.{2}f}/7", {1uz, 2uz, 3uz}, 5uz},
        {R"(f"{7}:{42:0{width}}")", "7:{0:0{1}}", {1uz, 2uz}, 3uz},
        {R"(f"{7}:{42:00}/{value}")", "7:{0:00}/{1}", {1uz, 2uz}, 3uz},
        {R"(f"{value}")", std::nullopt, {}, 1uz},
        {R"(f"{42}{::probe::value()}")", std::nullopt, {}, 2uz},
        {R"(f"{42}{1:65537}/{value}")", "42{0:65537}/{1}", {1uz, 2uz}, 3uz},
        {R"(f"{1:65536}/{value}")", std::nullopt, {}, 2uz},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.expression);
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
                    REQUIRE(selected_plan != nullptr);
                    const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                    ++count;
                    CHECK_FALSE(expression.constant.has_value());
                    CHECK_FALSE(std::holds_alternative<PreparedFormatText>(preparation));
                    CHECK(format->operands.size() == scenario.operands);
                    const auto* delegated = std::get_if<PreparedDelegatedFormat>(&preparation);
                    const auto indices = prepared_format_operands(preparation);
                    REQUIRE(
                        (indices.size() < format->operands.size()) == scenario.remainder.has_value()
                    );
                    if (scenario.remainder) {
                        CHECK(std::ranges::equal(indices, scenario.indices));
                        if (delegated != nullptr) {
                            CHECK(delegated->format_string == *scenario.remainder);
                        }
                    }
                }
            );
        }
        CHECK(count == 1uz);
    }
}

TEST_CASE("Format preparation: delegated residual byte budgets include escaped braces") {
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
                        REQUIRE(selected_plan != nullptr);
                        const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                        ++count;
                        CHECK(
                            (prepared_format_operands(preparation).size() < format->operands.size())
                            == (length == 32765uz)
                        );
                    }
                }
            );
        }
        CHECK(count == 1uz);
    }
}

TEST_CASE("Format preparation: delegated format strings preserve opaque braces") {
    const auto program = analyze_test_program(R"(
        fn format(value: i32) -> String => f"{42}/{value:\u{7b}}";
    )");
    auto checked = false;
    for (const auto callable : test_function_callables(program)) {
        const auto body = program.declarations().body_for_callable(callable);
        REQUIRE(body.has_value());
        visit_semantic_nodes(
            program.bodies().body(*body).region(),
            [&](const SemanticExpression& expression) noexcept {
                if (std::holds_alternative<SemFormat>(expression.value)) {
                    const auto selected_plan = prepare_operation(program, expression);
                    REQUIRE(selected_plan != nullptr);
                    const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                    const auto* delegated = std::get_if<PreparedDelegatedFormat>(&preparation);
                    REQUIRE(delegated != nullptr);
                    CHECK(delegated->format_string == "42/{0:{}");
                    CHECK(delegated->operand_indices == std::vector<std::size_t> {1});
                    CHECK(delegated->encoding == FormatResultEncoding::Unproven);
                    checked = true;
                }
            }
        );
    }
    CHECK(checked);
}
