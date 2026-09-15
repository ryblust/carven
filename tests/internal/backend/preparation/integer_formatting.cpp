module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.preparation.integer_formatting;

import :backend.preparation;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Format preparation: parsed residual integers keep all original source operands") {
    const auto program = analyze_test_program(R"(
        fn touch(&count: i32) -> bool { count += 1; return true; }
        fn format(&count: i32, value: u64) -> String {
            return f"{touch(&count) && false}/{{{value:016X}}}";
        }
    )");
    auto found = false;
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
                found = true;
                const auto* prepared = std::get_if<PreparedWriterFormat>(&preparation);
                REQUIRE(prepared != nullptr);
                CHECK(format->operands.size() == 2uz);
                CHECK(prepared->operand_indices == std::vector<std::size_t> {1uz});
                CHECK(prepared->format.text == std::vector<std::string> {"false/{", "}"});
                CHECK(prepared->format.minimum_size == 24u);
                CHECK(prepared->format.maximum_size == 24u);
            }
        );
    }
    CHECK(found);
}

TEST_CASE("Format preparation: direct residual text does not pay native brace escaping budget") {
    const auto source_text = std::string(32768uz, '{');
    auto escaped = std::string();
    for (const auto byte : source_text) {
        escaped += byte;
        escaped += byte;
    }
    const auto program = analyze_test_program(
        std::format("fn format(value: i32) -> String => f\"{}{{7}}/{{value}}\";", escaped)
    );
    auto found = false;
    for (const auto entry : program.bodies().entries()) {
        visit_semantic_nodes(
            entry.value.region(),
            [&](const SemanticExpression& expression) noexcept {
                if (!std::holds_alternative<SemFormat>(expression.value)) {
                    return;
                }
                const auto preparation = prepare_operation(program, expression);
                REQUIRE(preparation != nullptr);
                const auto* writer =
                    std::get_if<PreparedWriterFormat>(&std::get<PreparedFormat>(*preparation));
                REQUIRE(writer != nullptr);
                CHECK(writer->operand_indices == std::vector<std::size_t> {1uz});
                CHECK(writer->format.text.front() == source_text + "7/");
                found = true;
            }
        );
    }
    CHECK(found);
}
