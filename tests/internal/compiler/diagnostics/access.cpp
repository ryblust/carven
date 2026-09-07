module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.access;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: access failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {
            .name = "immutable assignment",
            .source = "fn invalid() { let value = 1; value = 2; }",
            .code = "CV-ACCESS-IMMUTABLE",
            .primary_text = "value",
        },
    });
    check_compiler_errors(cases);
}
