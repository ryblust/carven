module carven:test.internal.semantic.analysis.surface;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Declaration surfaces: C++ interfaces retain the ordinary module-domain audience"_test =
        [] static noexcept {
            constexpr auto signatures = std::to_array<std::string_view>({
                "fn value(input: Value) -> Value => input;",
                "fn value(input: [Value; 2]) -> [Value; 2] => input;",
                "fn value(input: ptr<Value>) -> ptr<Value> => input;",
                "fn value(input: fn(Value) -> Value) -> Value => input(Value {});",
                "fn value() => Value {};",
                "fn value() throw Failure { throw Failure::Invalid; }",
                "async fn value(input: Value) -> Value throw Failure => input;",
            });
            each(signatures, std::identity {}, [&](const auto& signature) noexcept {
                const auto types = std::string("struct Value {} enum Failure { Invalid, } ");
                expect(
                    analyze_test_errors(types + "export(cpp) " + std::string(signature)).empty()
                );
                expect_diagnostic(
                    analyze_test_errors(types + "export " + std::string(signature)),
                    DiagnosticCode::TypeVisibilityLeak
                );
            });
        };

    "Declaration surfaces: function result visibility follows the resolved type"_test =
        [] static noexcept {
            constexpr auto sources = std::array<std::string_view, 4uz> {
                "private struct Hidden {} export fn leak() -> Hidden { return Hidden {}; }",
                "private struct Hidden {} export fn leak() -> Hidden => Hidden {};",
                "private struct Hidden {} export fn leak() => Hidden {};",
                "private struct Hidden {} export fn leak() => ::native::wrap(Hidden {});",
            };
            each(sources, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(source));
                expect_diagnostic(diagnostics, DiagnosticCode::TypeVisibilityLeak);
            });
            const auto allowed =
                analyze_test_errors("export struct Visible {} export fn result() => Visible {};");
            expect(allowed.empty());
        };

    "Declaration surfaces: returned closures expose capture types and inferred failures"_test =
        [] static noexcept {
            const auto capture = analyze_test_errors(
                "private struct Hidden {} export fn leak() => []() { "
                "let hidden = Hidden {}; return [hidden]() => hidden; }();"
            );
            expect_diagnostic(capture, DiagnosticCode::TypeVisibilityLeak);
            const auto failures = analyze_test_errors(
                "private struct Hidden {} private fn fail() throw Hidden { throw Hidden {}; } "
                "export fn leak() => []() => fail()?;"
            );
            expect_diagnostic(failures, DiagnosticCode::TypeVisibilityLeak);
        };
});

} // namespace
