module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.interpolation;

import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic interpolation: holes normalize to ordered explicit format arguments") {
    const auto program =
        analyze_test_program(R"(fn format(value: f64, width: i32, precision: i32) -> String {
        return f"{{value}}={value:{width}.{precision}f}\0";
    })");
    const auto callable = test_function_callables(program).front();
    const auto body_id = program.declarations().body_for_callable(callable);
    REQUIRE(body_id.has_value());
    auto count = 0uz;
    visit_semantic_nodes(
        program.bodies().body(*body_id).region(),
        [&](const SemanticExpression& expression) noexcept {
            if (const auto* format = std::get_if<SemFormat>(&expression.value)) {
                ++count;
                REQUIRE(format->operands.size() == 3uz);
                CHECK(
                    std::ranges::all_of(
                        format->operands,
                        [](const SemCallArgument& operand) static noexcept {
                            return operand.access == AccessMode::Read;
                        }
                    )
                );
                const auto& constant = program.constants().constant(format->format_string_id);
                const auto* text = std::get_if<StringConstant>(&constant.value);
                REQUIRE(text != nullptr);
                CHECK(
                    program.provenance().spelling(text->value)
                    == std::string_view("{{value}}={0:{1}.{2}f}\0", 23)
                );
            }
        }
    );
    CHECK(count == 1uz);
}
