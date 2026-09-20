module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.styling;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.report;
import :source.manager;
import :source.text;
import std;

TEST_CASE("Diagnostic presentation: styling preserves the complete source diagnostic") {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("input.cv", "@");
    REQUIRE(source_id.has_value());
    auto builder = DiagnosticBuilder(DiagnosticCode::Lexical, "invalid token");
    builder.primary(locate(*source_id, Span::from_bounds(0, 1)));
    builder.note("expected a source token");
    const auto diagnostic = builder.build();

    const auto plain = render_diagnostic(diagnostic, sources, false);
    const auto styled = render_diagnostic(diagnostic, sources, true);
    // Only complete, non-nested style/reset pairs may be removed.
    const auto styled_text = std::regex("\\x1b\\[[0-9;]+m([^\\x1b]*)\\x1b\\[0m");
    CHECK_FALSE(plain.contains('\033'));
    CHECK_NE(styled, plain);
    CHECK_EQ(std::regex_replace(styled, styled_text, "$1"), plain);
}
