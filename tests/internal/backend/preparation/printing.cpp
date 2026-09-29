module carven:test.internal.backend.preparation.printing;

import :backend.preparation;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Print preparation: known scalar text retains the complete source operands",
        [] static noexcept {
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
                        if (!ct::expect(selected_plan != nullptr)) {
                            return;
                        }
                        const auto& prepared = std::get<PreparedPrint>(*selected_plan);
                        ++count;
                        if (!ct::expect(print->operands.size() == expected.size())) {
                            return;
                        }
                        if (!ct::expect(prepared.operand_text.size() == expected.size())) {
                            return;
                        }
                        for (const auto& [index, contents] : std::views::enumerate(expected)) {
                            const auto& text = prepared.operand_text[index];
                            ct::expect(text.has_value() == contents.has_value())
                                .note("index: ", index);
                            if (text && contents) {
                                ct::expect(*text == *contents).note("index: ", index);
                            }
                        }
                        ct::expect(
                            std::holds_alternative<SemBinding>(print->operands[0].expression.value)
                        );
                        ct::expect(
                            std::holds_alternative<SemShortCircuit>(
                                print->operands[3].expression.value
                            )
                        );
                        ct::expect(
                            std::holds_alternative<SemFormat>(print->operands[5].expression.value)
                        );
                    }
                );
            }
            ct::expect(count == 1uz);
        }
    );

    ct::test(
        "Print preparation: dynamic operands and empty calls need no prepared text",
        [] static noexcept {
            const auto program =
                analyze_test_program("fn output(value: i32) { println(value); println(); }");
            auto count = 0uz;
            for (const auto entry : program.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (std::holds_alternative<SemPrint>(expression.value)) {
                            ct::expect(prepare_operation(program, expression) == nullptr);
                            ++count;
                        }
                    }
                );
            }
            ct::expect(count == 2uz);
        }
    );
});

} // namespace
