module carven:test.internal.backend.preparation.formatted_append;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Format preparation: append shares normalization with a separate Write destination"_test =
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
                        if (!expect(selected_plan != nullptr)) {
                            return;
                        }
                        const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                        if (!expect(format->receiver.has_value())) {
                            return;
                        }
                        expect((**format->receiver).category == SemanticValueCategory::Place);
                        expect(
                            program.types().type((**format->receiver).type.resolved()).value
                            == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}
                        );
                        expect(
                            program.types().type(expression.type.resolved()).value
                            == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}}
                        );
                        expect(!(expression.constant.has_value()));
                        if (count == 0uz) {
                            if (!expect(format->operands.size() == 3uz)) {
                                return;
                            }
                            expect(serialize_format(format->specification) == "{0}/{1:0{2}}");
                            const auto* writer = std::get_if<PreparedWriterFormat>(&preparation);
                            if (!expect(writer != nullptr)) {
                                return;
                            }
                            expect(writer->format.text == std::vector<std::string> {"7/", ""});
                            expect(writer->operand_indices == std::vector<std::size_t> {1uz, 2uz});
                            const auto* integer =
                                std::get_if<IntegerFormatField>(&writer->format.fields.front());
                            if (!expect(integer != nullptr)) {
                                return;
                            }
                            expect(!(integer->static_width.has_value()));
                        } else if (count == 1uz) {
                            const auto* text = std::get_if<PreparedFormatText>(&preparation);
                            if (!expect(text != nullptr)) {
                                return;
                            }
                            expect(text->text == "7");
                            expect(format->operands.size() == 1uz);
                        } else {
                            expect(std::holds_alternative<PreparedWriterFormat>(preparation));
                        }
                        ++count;
                    }
                );
            }
            expect(count == 3uz);
        };
});

} // namespace
