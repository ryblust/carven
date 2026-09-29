module carven:test.internal.backend.preparation.formatted_append;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Format preparation: append shares normalization with a separate Write destination",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn add(&text: String, value: i32, width: i32) {
            text.append_format(f"{7}/{value:0{width}}");
            text.append_format(f"{7}");
            text.append_format(f"{value:04}");
        }
    )");
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
                        if (!ct::expect(selected_plan != nullptr)) {
                            return;
                        }
                        const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                        if (!ct::expect(format->receiver.has_value())) {
                            return;
                        }
                        ct::expect((**format->receiver).category == SemanticValueCategory::Place);
                        ct::expect(
                            program.types().type((**format->receiver).type.resolved()).value
                            == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}
                        );
                        ct::expect(
                            program.types().type(expression.type.resolved()).value
                            == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}}
                        );
                        ct::expect(!(expression.constant.has_value()));
                        if (count == 0uz) {
                            if (!ct::expect(format->operands.size() == 3uz)) {
                                return;
                            }
                            ct::expect(serialize_format(format->specification) == "{0}/{1:0{2}}");
                            const auto* writer = std::get_if<PreparedWriterFormat>(&preparation);
                            if (!ct::expect(writer != nullptr)) {
                                return;
                            }
                            ct::expect(writer->format.text == std::vector<std::string> {"7/", ""});
                            ct::expect(
                                writer->operand_indices == std::vector<std::size_t> {1uz, 2uz}
                            );
                            const auto* integer =
                                std::get_if<IntegerFormatField>(&writer->format.fields.front());
                            if (!ct::expect(integer != nullptr)) {
                                return;
                            }
                            ct::expect(!(integer->static_width.has_value()));
                        } else if (count == 1uz) {
                            const auto* text = std::get_if<PreparedFormatText>(&preparation);
                            if (!ct::expect(text != nullptr)) {
                                return;
                            }
                            ct::expect(text->text == "7");
                            ct::expect(format->operands.size() == 1uz);
                        } else {
                            ct::expect(std::holds_alternative<PreparedWriterFormat>(preparation));
                        }
                        ++count;
                    }
                );
            }
            ct::expect(count == 3uz);
        }
    );
});

} // namespace
