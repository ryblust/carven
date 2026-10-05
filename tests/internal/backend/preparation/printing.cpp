module carven:test.internal.backend.preparation.printing;

import :backend.preparation;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Print preparation: static scalar text preserves ordinary locals and runtime operands"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "fn touch() -> bool { return true; } "
                "fn output(dynamic: i32) { let known = 42; const folded = 42; var changed = 3; "
                "println(known, true, '我', touch() && false, \"raw\", f\"{7}\", 1.25, dynamic, changed, folded); }"
            );
            const auto expected = std::array<std::optional<std::string_view>, 10uz> {
                std::nullopt,
                "true",
                "我",
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "1.25",
                std::nullopt,
                std::nullopt,
                "42",
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
                        if (!expect(selected_plan != nullptr)) {
                            return;
                        }
                        const auto& prepared = std::get<PreparedPrint>(*selected_plan);
                        ++count;
                        if (!expect(print->operands.size() == expected.size())) {
                            return;
                        }
                        if (!expect(prepared.operand_text.size() == expected.size())) {
                            return;
                        }
                        for (const auto& [index, contents] : std::views::enumerate(expected)) {
                            const auto& text = prepared.operand_text[index];
                            expect(text.has_value() == contents.has_value()).note("index: ", index);
                            if (text && contents) {
                                expect(*text == *contents).note("index: ", index);
                            }
                        }
                        expect(
                            std::holds_alternative<SemBinding>(print->operands[0].expression.value)
                        );
                        expect(
                            std::holds_alternative<SemShortCircuit>(
                                print->operands[3].expression.value
                            )
                        );
                        expect(
                            std::holds_alternative<SemFormat>(print->operands[5].expression.value)
                        );
                    }
                );
            }
            expect(count == 1uz);
        };

    "Print preparation: dynamic operands and empty calls need no prepared text"_test =
        [] static noexcept {
            const auto program =
                analyze_test_program("fn output(value: i32) { println(value); println(); }");
            auto count = 0uz;
            for (const auto entry : program.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (std::holds_alternative<SemPrint>(expression.value)) {
                            expect(prepare_operation(program, expression) == nullptr);
                            ++count;
                        }
                    }
                );
            }
            expect(count == 2uz);
        };
});

} // namespace
