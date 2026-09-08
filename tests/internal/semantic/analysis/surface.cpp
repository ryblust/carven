module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.surface;

import :diagnostics.code;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Declaration surfaces: function result visibility follows the resolved type") {
    const auto sources = std::array<std::string_view, 4uz> {
        "private struct Hidden {} export fn leak() -> Hidden { return Hidden {}; }",
        "private struct Hidden {} export fn leak() -> Hidden => Hidden {};",
        "private struct Hidden {} export fn leak() => Hidden {};",
        "private struct Hidden {} export fn leak() => ::native::wrap(Hidden {});",
    };
    for (const auto source : sources) {
        CAPTURE(source);
        const auto diagnostics = analyze_test_errors(std::string(source));
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::TypeVisibilityLeak));
    }
    const auto allowed =
        analyze_test_errors("export struct Visible {} export fn result() => Visible {};");
    CHECK(allowed.empty());
}

TEST_CASE("Declaration surfaces: returned closures expose capture types and inferred failures") {
    const auto capture = analyze_test_errors(
        "private struct Hidden {} export fn leak() => []() { "
        "let hidden = Hidden {}; return [hidden]() => hidden; }();"
    );
    CHECK(contains_diagnostic_code(capture, DiagnosticCode::TypeVisibilityLeak));
    const auto failures = analyze_test_errors(
        "private struct Hidden {} private fn fail() throw Hidden { throw Hidden {}; } "
        "export fn leak() => []() => fail()?;"
    );
    CHECK(contains_diagnostic_code(failures, DiagnosticCode::TypeVisibilityLeak));
}
