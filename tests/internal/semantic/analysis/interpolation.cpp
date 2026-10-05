module carven:test.internal.semantic.analysis.interpolation;

import :semantic.format;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Semantic interpolation: holes normalize to ordered explicit format arguments"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                R"(fn format(value: f64, width: i32, precision: i32) -> String {
        return f"{{value}}={value:{width}.{precision}f}\0";
    })"
            );
            const auto callable = test_function_callables(program).front();
            const auto body_id = program.declarations().body_for_callable(callable);
            if (!expect(body_id.has_value())) {
                return;
            }
            auto count = 0uz;
            visit_semantic_nodes(
                program.bodies().body(*body_id).region(),
                [&](const SemanticExpression& expression) noexcept {
                    if (const auto* format = std::get_if<SemFormat>(&expression.value)) {
                        ++count;
                        if (!expect(format->operands.size() == 3uz)) {
                            return;
                        }
                        expect(
                            std::ranges::all_of(
                                format->operands,
                                [](const SemCallArgument& operand) static noexcept {
                                    return operand.access == AccessMode::Read;
                                }
                            )
                        );
                        expect(
                            serialize_format(format->specification)
                            == std::string_view("{{value}}={0:{1}.{2}f}\0", 23)
                        );
                    }
                }
            );
            expect(count == 1uz);
        };
});

} // namespace
