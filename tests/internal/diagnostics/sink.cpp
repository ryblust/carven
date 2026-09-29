module carven:test.internal.diagnostics.sink;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.sink;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Diagnostic: sink distinguishes warnings from blocking errors", [] static noexcept {
        auto sink = DiagnosticSink();
        sink.emit(DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "warning").build());
        ct::expect(!sink.has_errors());
        sink.emit(DiagnosticBuilder(DiagnosticCode::Lexical, "error").build());
        ct::expect(sink.has_errors());
        ct::expect_equal(sink.values().size(), 2u);
    });
});

} // namespace
