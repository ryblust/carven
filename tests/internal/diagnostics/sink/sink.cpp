module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.sink;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.sink;
import std;

TEST_CASE("Diagnostic: sink distinguishes warnings from blocking errors") {
    auto sink = DiagnosticSink();
    sink.emit(DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "warning").build());
    CHECK(!sink.has_errors());
    sink.emit(DiagnosticBuilder(DiagnosticCode::Lexical, "error").build());
    CHECK(sink.has_errors());
    CHECK_EQ(sink.values().size(), 2u);
}
