module carven:test.internal.diagnostics.sink;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.sink;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Diagnostic: sink distinguishes warnings from blocking errors"_test = [] static noexcept {
        auto sink = DiagnosticSink();
        sink.emit(DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "warning").build());
        expect(!sink.has_errors());
        sink.emit(DiagnosticBuilder(DiagnosticCode::Lexical, "error").build());
        expect(sink.has_errors());
        expect_equal(sink.values().size(), 2u);
    };
});

} // namespace
