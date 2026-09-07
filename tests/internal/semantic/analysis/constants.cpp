module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constants;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic constants: declarations publish values without executable bodies") {
    const auto program = analyze_test_program(
        "enum Choice { Value(i32), Empty, }\n"
        "enum State: u8 { Ready = 4, Done, }\n"
        "const arithmetic: i64 = -2i64 + 5;\n"
        "const raw: i32 = State::Done as i32;\n"
        "const text_size: usize = \"abc\".len();\n"
        "const same = Choice::Value(2) == .Value(2);\n"
    );
    CHECK_EQ(program.bodies().size(), 0uz);
    auto values = std::vector<ConstantID>();
    for (const auto [id, declaration] : program.declarations().module_constants()) {
        static_cast<void>(id);
        values.push_back(declaration.value);
    }
    REQUIRE_EQ(values.size(), 4uz);
    const auto expected_integers = std::array {3ll, 5ll, 3ll};
    for (auto index = 0uz; index < expected_integers.size(); ++index) {
        const auto* integer =
            std::get_if<IntegerConstant>(&program.constants().constant(values[index]).value);
        REQUIRE(integer != nullptr);
        CHECK_EQ(integer->as_signed(), expected_integers[index]);
    }
    const auto* equality =
        std::get_if<BooleanConstant>(&program.constants().constant(values.back()).value);
    REQUIRE(equality != nullptr);
    CHECK(equality->value);
}

TEST_CASE("Semantic constants: known results do not broaden static syntax") {
    const auto diagnostics = analyze_test_errors(
        "fn source() -> i32 { return 1; } "
        "fn invalid() { const _ = (source() == 1) && false; }"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstInitializer));
}
