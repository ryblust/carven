module carven:test.internal.semantic.analysis.surface;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Declaration surfaces: function result visibility follows the resolved type",
        [] static noexcept {
            const auto sources = std::array<std::string_view, 4uz> {
                "private struct Hidden {} export fn leak() -> Hidden { return Hidden {}; }",
                "private struct Hidden {} export fn leak() -> Hidden => Hidden {};",
                "private struct Hidden {} export fn leak() => Hidden {};",
                "private struct Hidden {} export fn leak() => ::native::wrap(Hidden {});",
            };
            ct::each(sources, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(source));
                ct::expect_diagnostic(diagnostics, DiagnosticCode::TypeVisibilityLeak);
            });
            const auto allowed =
                analyze_test_errors("export struct Visible {} export fn result() => Visible {};");
            ct::expect(allowed.empty());
        }
    );

    ct::test(
        "Declaration surfaces: returned closures expose capture types and inferred failures",
        [] static noexcept {
            const auto capture = analyze_test_errors(
                "private struct Hidden {} export fn leak() => []() { "
                "let hidden = Hidden {}; return [hidden]() => hidden; }();"
            );
            ct::expect_diagnostic(capture, DiagnosticCode::TypeVisibilityLeak);
            const auto failures = analyze_test_errors(
                "private struct Hidden {} private fn fail() throw Hidden { throw Hidden {}; } "
                "export fn leak() => []() => fail()?;"
            );
            ct::expect_diagnostic(failures, DiagnosticCode::TypeVisibilityLeak);
        }
    );
});

} // namespace
