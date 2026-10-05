module carven:test.internal.diagnostics.diagnosed;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Diagnostic: values with diagnostics retain non-blocking diagnostics"_test =
        [] static noexcept {
            const auto diagnosed = Diagnosed<int> {
                .value = 42,
                .diagnostics = {
                    DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "warning").build(),
                },
            };
            expect_equal(diagnosed.value, 42);
            expect(!has_errors(diagnosed));
        };
});

} // namespace
