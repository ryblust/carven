module carven:test.internal.diagnostics.collection;

import :diagnostics.report;
import :source.text;
import :test.harness.framework;
import :test.internal.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Diagnostic report: collections preserve order and use one separator",
        [] static noexcept {
            static constexpr auto source = SourceView {
                .source_id = SourceID::from_index(0),
                .text = "@",
                .origin = "bad.cv",
            };
            const auto diagnostics = std::array {
                make_diagnostic(
                    "first",
                    {
                        .span =
                            {
                                .source_id = SourceID::from_index(0),
                                .span = Span::from_bounds(0, 1),
                            },
                        .message = {},
                    }
                ),
                make_diagnostic(
                    "second",
                    {
                        .span =
                            {
                                .source_id = SourceID::from_index(0),
                                .span = Span::from_bounds(1, 1),
                            },
                        .message = {},
                    }
                ),
            };

            ct::expect(render_diagnostics({}, source).empty());
            const auto rendered = render_diagnostics(diagnostics, source);
            const auto first = rendered.find("first");
            const auto second = rendered.find("second");
            if (!ct::expect(first != std::string::npos)) {
                return;
            }
            if (!ct::expect(second != std::string::npos)) {
                return;
            }
            ct::expect(first < second);
            const auto separator = rendered.find("\n\n", first);
            if (!ct::expect(separator != std::string::npos)) {
                return;
            }
            ct::expect_equal(separator, rendered.rfind("\n\n"));
        }
    );
});

} // namespace
