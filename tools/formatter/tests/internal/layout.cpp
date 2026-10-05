module carven:test.formatter.layout;

import :formatter.layout.document;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Formatter layout: groups include suffix punctuation in their width budget"_test =
        [] static noexcept {
            auto doc = FormattingDocument();
            const auto args = doc.group(doc.concat(
                {doc.text("("),
                 doc.indent(doc.concat(
                     {doc.line(false), doc.text("alpha,"), doc.line(true), doc.text("beta")}
                 )),
                 doc.line(false),
                 doc.text(")")}
            ));
            const auto root = doc.concat({doc.text("f"), args, doc.text(";"), doc.hardline()});
            expect_equal(doc.render(root, 15), std::string_view("f(alpha, beta);\n"));
            expect_equal(doc.render(root, 14), std::string_view("f(\n    alpha,\n    beta\n);\n"));
        };

    "Formatter layout: nested groups choose layouts independently"_test = [] static noexcept {
        auto doc = FormattingDocument();
        const auto inner = doc.group(doc.concat(
            {doc.text("g("),
             doc.indent(
                 doc.concat({doc.line(false), doc.text("a,"), doc.line(true), doc.text("b")})
             ),
             doc.line(false),
             doc.text(")")}
        ));
        const auto outer = doc.group(doc.concat(
            {doc.text("f("),
             doc.indent(doc.concat(
                 {doc.line(false), inner, doc.text(","), doc.line(true), doc.text("other")}
             )),
             doc.line(false),
             doc.text(")")}
        ));
        expect_equal(doc.render(outer, 12), std::string_view("f(\n    g(a, b),\n    other\n)"));
        expect_equal(
            doc.render(outer, 10),
            std::string_view("f(\n    g(\n        a,\n        b\n    ),\n    other\n)")
        );
    };

    "Formatter layout: hard breaks prevent flattening and blank lines have no indentation"_test =
        [] static noexcept {
            auto doc = FormattingDocument();
            const auto root = doc.group(doc.concat(
                {doc.text("{"),
                 doc.indent(doc.concat(
                     {doc.hardline(),
                      doc.text("// comment"),
                      doc.hardline(),
                      doc.hardline(),
                      doc.text("x")}
                 )),
                 doc.hardline(),
                 doc.text("}")}
            ));
            expect_equal(doc.render(root), std::string_view("{\n    // comment\n\n    x\n}"));
        };

    "Formatter layout: verbatim bytes bypass internal indentation and newline normalization"_test =
        [] static noexcept {
            auto doc = FormattingDocument();
            const auto root = doc.concat(
                {doc.text("{"),
                 doc.indent(doc.concat(
                     {doc.hardline(), doc.verbatim("#[cpp] ---\r\n\t// foreign  \r\n---")}
                 )),
                 doc.hardline(),
                 doc.text("}")}
            );
            expect_equal(
                doc.render(root, 4),
                std::string_view("{\n    #[cpp] ---\r\n\t// foreign  \r\n---\n}")
            );
            const auto long_token = doc.text("indivisible_token");
            expect_equal(doc.render(long_token, 1), std::string_view("indivisible_token"));
        };

    "Formatter layout: deeply nested documents preserve output"_test = [] static noexcept {
        auto doc = FormattingDocument();
        auto root = doc.text("x");
        for (auto index = 0uz; index < 4096uz; ++index) {
            root = doc.group(doc.concat({doc.text("("), root, doc.text(")")}));
        }
        const auto expected = std::string(4096, '(') + "x" + std::string(4096, ')');
        expect_equal(doc.render(root, 1), expected);
    };

    "Formatter layout: source line breaks do not request generated indentation"_test =
        [] static noexcept {
            auto doc = FormattingDocument();
            const auto source = std::string_view("first\r\nsecond\n");
            const auto layout = [&](std::vector<FormattingNodeID> fragments) noexcept {
                return doc.concat({
                    doc.text("{"),
                    doc.indent(doc.concat({
                        doc.hardline(),
                        doc.concat(std::move(fragments)),
                        doc.text("tail"),
                        doc.hardline(),
                        doc.text("generated"),
                    })),
                    doc.hardline(),
                    doc.text("}"),
                });
            };
            const auto expected = "{\n    first\r\nsecond\ntail\n    generated\n}";
            expect_equal(doc.render(layout({doc.verbatim(source)})), expected);
            for (auto split = 1uz; split < source.size(); ++split) {
                expect_equal(
                    doc.render(layout(
                        {doc.verbatim(source.substr(0, split)), doc.verbatim(source.substr(split))}
                    )),
                    expected
                )
                    .note("split at byte ", split);
            }
        };
});

} // namespace
