module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constant_array_initializers;

import :diagnostics.code;
import :semantic.semir.constant;
import :semantic.semir.program;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Const arrays: initializer queries compose literals and typed function results") {
    const auto program = analyze_test_program(R"(
        const texts: [str; 1] = [String::from_str("hi")];
        const nested_texts: [[str; 1]; 1] = [[String::from_str("nested")]];
        const converted = texts[0] == "hi" && nested_texts[0][0] == "nested";
        const fn row(value: u16) -> [u16; 2] => [value, value + 1u16];
        const nested = [row(4u16), row(8u16)];
        const selected = nested[1][0];
        const direct = row(4u16)[1];
        const equal = row(4u16) == [4u16, 5u16];
        const different = [[1, 2]] != [[1, 3]];
        const fn selected_index() -> i32 => 1;
        const skipped = false && ([1, 2][selected_index()] == 0);
        const skipped_literal = false && ([1, 2][1] == 0);
    )");
    auto checked = 0uz;
    for (const auto [id, declaration] : program.declarations().module_constants()) {
        static_cast<void>(id);
        const auto name = program.provenance().spelling(declaration.name);
        const auto& fact = program.constants().constant(declaration.value);
        if (name == "selected" || name == "direct") {
            const auto* integer = std::get_if<IntegerConstant>(&fact.value);
            REQUIRE(integer != nullptr);
            CHECK(integer->as_signed() == (name == "selected" ? 8 : 5));
            ++checked;
        } else if (name == "equal"
                   || name == "converted"
                   || name == "different"
                   || name == "skipped"
                   || name == "skipped_literal") {
            const auto* boolean = std::get_if<BooleanConstant>(&fact.value);
            REQUIRE(boolean != nullptr);
            CHECK(boolean->value == (name != "skipped" && name != "skipped_literal"));
            ++checked;
        }
    }
    CHECK(checked == 7uz);
}

TEST_CASE("Const arrays: literal context and indexing retain source diagnostics") {
    struct Case final {
        std::string_view source;
        DiagnosticCode code;
    };

    const auto cases = std::array {
        Case {.source = "const values = [];", .code = DiagnosticCode::TypeEmptyArray},
        Case {.source = "const values: [i32; 2] = [1];", .code = DiagnosticCode::TypeMismatch},
        Case {.source = "const value = [1, 2][2];", .code = DiagnosticCode::ConstIndexBounds},
        Case {.source = "const value = [1, 2][-1];", .code = DiagnosticCode::ConstIndexBounds},
        Case {
            .source = "const value = false && ([1, 2][true] == 0);",
            .code = DiagnosticCode::TypeIndexInteger,
        },
        Case {
            .source = "const values: [String; 1] = [\"text\"];",
            .code = DiagnosticCode::ConstInitializer
        },
    };
    for (const auto& item : cases) {
        INFO(item.source);
        const auto diagnostics = analyze_test_errors(std::string(item.source));
        CHECK(contains_diagnostic_code(diagnostics, item.code));
    }
}

TEST_CASE("Const arrays: repeated constant children still count toward the complete value budget") {
    auto source = std::string("const row = [");
    for (auto index = 0uz; index < 260uz; ++index) {
        source += index == 0uz ? "0" : ",0";
    }
    source += "]; const table = [";
    for (auto index = 0uz; index < 260uz; ++index) {
        source += index == 0uz ? "row" : ",row";
    }
    source += "];";
    const auto diagnostics = analyze_test_errors(std::move(source));
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstLimit));
}
