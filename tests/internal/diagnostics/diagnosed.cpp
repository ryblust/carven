module carven:test.internal.diagnostics.diagnosed;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Diagnostic: values with diagnostics retain non-blocking diagnostics",
        [] static noexcept {
            const auto diagnosed = Diagnosed<int> {
                .value = 42,
                .diagnostics = {
                    DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "warning").build(),
                },
            };
            ct::expect_equal(diagnosed.value, 42);
            ct::expect(!has_errors(diagnosed));
        }
    );
});

} // namespace
