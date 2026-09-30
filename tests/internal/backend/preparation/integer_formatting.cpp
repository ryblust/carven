module carven:test.internal.backend.preparation.integer_formatting;

import :backend.preparation;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Format preparation: parsed residual integers keep all original source operands",
        [] static noexcept {
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
                        if (!ct::expect(selected_plan != nullptr)) {
                            return;
                        }
                        const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                        found = true;
                        const auto* prepared = std::get_if<PreparedWriterFormat>(&preparation);
                        if (!ct::expect(prepared != nullptr)) {
                            return;
                        }
                        ct::expect(format->operands.size() == 2uz);
                        ct::expect(
                            prepared->operand_indices == std::vector<std::size_t> {0uz, 1uz}
                        );
                        ct::expect(
                            prepared->format.text == std::vector<std::string> {"", "/{", "}"}
                        );
                        ct::expect(prepared->format.minimum_size == 23u);
                        ct::expect(prepared->format.maximum_size == 24u);
                    }
                );
            }
            ct::expect(found);
        }
    );

    ct::test(
        "Format preparation: direct residual text does not pay native brace escaping budget",
        [] static noexcept {
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
                        if (!ct::expect(preparation != nullptr)) {
                            return;
                        }
                        const auto* writer = std::get_if<PreparedWriterFormat>(
                            &std::get<PreparedFormat>(*preparation)
                        );
                        if (!ct::expect(writer != nullptr)) {
                            return;
                        }
                        ct::expect(writer->operand_indices == std::vector<std::size_t> {1uz});
                        ct::expect(writer->format.text.front() == source_text + "7/");
                        found = true;
                    }
                );
            }
            ct::expect(found);
        }
    );
});

} // namespace
