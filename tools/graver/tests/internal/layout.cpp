module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.graver.layout;

import :graver.layout.document;
import std;

TEST_CASE("Graver layout: groups include suffix punctuation in their width budget") {
    auto doc = graver::Document();
    const auto args = doc.group(doc.concat(
        {doc.text("("),
         doc.indent(
             doc.concat({doc.line(false), doc.text("alpha,"), doc.line(true), doc.text("beta")})
         ),
         doc.line(false),
         doc.text(")")}
    ));
    const auto root = doc.concat({doc.text("f"), args, doc.text(";"), doc.hardline()});
    CHECK(doc.render(root, 15) == "f(alpha, beta);\n");
    CHECK(doc.render(root, 14) == "f(\n    alpha,\n    beta\n);\n");
}

TEST_CASE("Graver layout: nested groups choose layouts independently") {
    auto doc = graver::Document();
    const auto inner = doc.group(doc.concat(
        {doc.text("g("),
         doc.indent(doc.concat({doc.line(false), doc.text("a,"), doc.line(true), doc.text("b")})),
         doc.line(false),
         doc.text(")")}
    ));
    const auto outer = doc.group(doc.concat(
        {doc.text("f("),
         doc.indent(
             doc.concat({doc.line(false), inner, doc.text(","), doc.line(true), doc.text("other")})
         ),
         doc.line(false),
         doc.text(")")}
    ));
    CHECK(doc.render(outer, 12) == "f(\n    g(a, b),\n    other\n)");
    CHECK(doc.render(outer, 10) == "f(\n    g(\n        a,\n        b\n    ),\n    other\n)");
}

TEST_CASE("Graver layout: hard breaks prevent flattening and blank lines have no indentation") {
    auto doc = graver::Document();
    const auto root = doc.group(doc.concat(
        {doc.text("{"),
         doc.indent(doc.concat(
             {doc.hardline(), doc.text("// comment"), doc.hardline(), doc.hardline(), doc.text("x")}
         )),
         doc.hardline(),
         doc.text("}")}
    ));
    CHECK(doc.render(root) == "{\n    // comment\n\n    x\n}");
}

TEST_CASE("Graver layout: verbatim bytes bypass internal indentation and newline normalization") {
    auto doc = graver::Document();
    const auto root = doc.concat(
        {doc.text("{"),
         doc.indent(
             doc.concat({doc.hardline(), doc.verbatim("#[cpp] ---\r\n\t// foreign  \r\n---")})
         ),
         doc.hardline(),
         doc.text("}")}
    );
    CHECK(doc.render(root, 4) == "{\n    #[cpp] ---\r\n\t// foreign  \r\n---\n}");
    const auto long_token = doc.text("indivisible_token");
    CHECK(doc.render(long_token, 1) == "indivisible_token");
}

TEST_CASE("Graver layout: deeply nested documents preserve output") {
    auto doc = graver::Document();
    auto root = doc.text("x");
    for (auto index = 0uz; index < 4096uz; ++index) {
        root = doc.group(doc.concat({doc.text("("), root, doc.text(")")}));
    }
    const auto expected = std::string(4096, '(') + "x" + std::string(4096, ')');
    CHECK(doc.render(root, 1) == expected);
}
