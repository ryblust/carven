module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.preparation.printing;

import :backend.preparation;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Print preparation: known scalar text retains the complete source operands") {
    const auto program = analyze_test_program(
        "fn touch() -> bool { return true; } "
        "fn output(dynamic: i32) { let known = 42; var changed = 3; "
        "println(known, true, '我', touch() && false, \"raw\", f\"{7}\", 1.25, dynamic, changed); }"
    );
    const auto expected = std::array<std::optional<std::string_view>, 9uz> {
        "42",
        "true",
        "我",
        "false",
        std::nullopt,
        std::nullopt,
        "1.25",
        std::nullopt,
        std::nullopt,
    };
    auto count = 0uz;
    for (const auto entry : program.bodies().entries()) {
        visit_semantic_nodes(
            entry.value.region(),
            [&](const SemanticExpression& expression) noexcept {
                const auto* print = std::get_if<SemPrint>(&expression.value);
                if (print == nullptr) {
                    return;
                }
                const auto selected_plan = prepare_operation(program, expression);
                REQUIRE(selected_plan != nullptr);
                const auto& prepared = std::get<PreparedPrint>(*selected_plan);
                ++count;
                REQUIRE(print->operands.size() == expected.size());
                REQUIRE(prepared.operand_text.size() == expected.size());
                for (const auto& [index, contents] : std::views::enumerate(expected)) {
                    CAPTURE(index);
                    const auto& text = prepared.operand_text[index];
                    CHECK(text.has_value() == contents.has_value());
                    if (text && contents) {
                        CHECK(*text == *contents);
                    }
                }
                CHECK(std::holds_alternative<SemBinding>(print->operands[0].expression.value));
                CHECK(std::holds_alternative<SemShortCircuit>(print->operands[3].expression.value));
                CHECK(std::holds_alternative<SemFormat>(print->operands[5].expression.value));
            }
        );
    }
    CHECK(count == 1uz);
}

TEST_CASE("Print preparation: dynamic operands and empty calls need no prepared text") {
    const auto program =
        analyze_test_program("fn output(value: i32) { println(value); println(); }");
    auto count = 0uz;
    for (const auto entry : program.bodies().entries()) {
        visit_semantic_nodes(
            entry.value.region(),
            [&](const SemanticExpression& expression) noexcept {
                if (std::holds_alternative<SemPrint>(expression.value)) {
                    CHECK(prepare_operation(program, expression) == nullptr);
                    ++count;
                }
            }
        );
    }
    CHECK(count == 2uz);
}
