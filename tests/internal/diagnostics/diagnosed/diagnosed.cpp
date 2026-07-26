module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.diagnosed;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnosed;
import std;

TEST_CASE("Diagnostic: values with diagnostics retain non-blocking diagnostics") {
    const auto diagnosed = Diagnosed<int> {
        .value = 42,
        .diagnostics = {
            DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "warning").build(),
        },
    };
    CHECK_EQ(diagnosed.value, 42);
    CHECK(!has_errors(diagnosed));
}
