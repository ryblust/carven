module carven:test.internal.diagnostics.styling;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.report;
import :source.manager;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Diagnostic presentation: styling preserves the complete source diagnostic",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = sources.append_virtual("input.cv", "@");
            if (!ct::expect(source_id.has_value())) {
                return;
            }
            auto builder = DiagnosticBuilder(DiagnosticCode::Lexical, "invalid token");
            builder.primary(locate(*source_id, Span::from_bounds(0, 1)));
            builder.note("expected a source token");
            const auto diagnostic = builder.build();

            const auto plain = render_diagnostic(diagnostic, sources, false);
            const auto styled = render_diagnostic(diagnostic, sources, true);
            // Only complete, non-nested style/reset pairs may be removed.
            const auto styled_text = std::regex("\\x1b\\[[0-9;]+m([^\\x1b]*)\\x1b\\[0m");
            ct::expect(!(plain.contains('\033')));
            ct::expect_not_equal(styled, plain);
            ct::expect_equal(std::regex_replace(styled, styled_text, "$1"), plain);
        }
    );
});

} // namespace
