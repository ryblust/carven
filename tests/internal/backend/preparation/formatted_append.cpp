module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.preparation.formatted_append;

import :backend.preparation;
import :semantic.format;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Format preparation: append shares normalization with a separate Write destination") {
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
                REQUIRE(selected_plan != nullptr);
                const auto& preparation = std::get<PreparedFormat>(*selected_plan);
                REQUIRE(format->receiver.has_value());
                CHECK((**format->receiver).category == SemanticValueCategory::Place);
                CHECK(
                    program.types().type((**format->receiver).type.resolved()).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}
                );
                CHECK(
                    program.types().type(expression.type.resolved()).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}}
                );
                CHECK_FALSE(expression.constant.has_value());
                if (count == 0uz) {
                    REQUIRE(format->operands.size() == 3uz);
                    CHECK(serialize_format(format->specification) == "{0}/{1:0{2}}");
                    const auto* delegated = std::get_if<PreparedDelegatedFormat>(&preparation);
                    REQUIRE(delegated != nullptr);
                    CHECK(delegated->format_string == "7/{0:0{1}}");
                    CHECK(delegated->operand_indices == std::vector<std::size_t> {1uz, 2uz});
                    CHECK(delegated->encoding == FormatResultEncoding::Unproven);
                } else if (count == 1uz) {
                    const auto* text = std::get_if<PreparedFormatText>(&preparation);
                    REQUIRE(text != nullptr);
                    CHECK(text->text == "7");
                    CHECK(format->operands.size() == 1uz);
                } else {
                    CHECK(std::holds_alternative<PreparedWriterFormat>(preparation));
                }
                ++count;
            }
        );
    }
    CHECK(count == 3uz);
}
