module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.dump.render;

import :frontend.ast.tree;
import :frontend.dump.ast;
import :frontend.dump.text;
import :frontend.dump.tokens;
import :frontend.lex;
import :frontend.lex.token;
import :frontend.parse;
import :source.manager;
import :source.text;
import std;

namespace {

struct DumpSource final {
    SourceManager sources;
    SourceID source_id;
};

auto dump_source(std::string origin, std::string text) noexcept -> DumpSource {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual(std::move(origin), std::move(text));
    return {.sources = std::move(sources), .source_id = source_id};
}

} // namespace

TEST_CASE("Dump: token output exposes ordered tokens and ranges") {
    const auto owned = dump_source("main.cv", "fn main() {}");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());

    const auto output = render_token_dump(owned.sources, lexical.value);
    const auto fn = output.find("Fn [0, 2) \"fn\"");
    const auto name = output.find("Identifier [3, 7) \"main\"");
    const auto body = output.find("RightBrace [11, 12) \"}\"");
    REQUIRE(fn != std::string::npos);
    REQUIRE(name != std::string::npos);
    REQUIRE(body != std::string::npos);
    CHECK(fn < name);
    CHECK(name < body);
    CHECK(!output.contains('\x1b'));
}

TEST_CASE("Dump: AST output exposes grammar fields without implementation details") {
    const auto owned = dump_source("main.cv", "fn main() {}");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("SourceModule [0, 12) \"main.cv\""));
    CHECK(output.contains("imports (0)"));
    CHECK(output.contains("items (1)"));
    CHECK(output.contains("FunctionDeclaration [0, 12)"));
    CHECK(output.contains("name [3, 7) \"main\""));
    CHECK(output.contains("parameters (0)"));
    CHECK(output.contains("body OrdinaryBlock [10, 12)"));
    CHECK(output.contains("statements (0)"));
    CHECK(!output.contains('\x1b'));
    CHECK(!output.contains("0x"));
}

TEST_CASE("Dump: imports expose structured module references") {
    const auto owned = dump_source("main.cv", "import math.vector using *;");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("imports (1)"));
    CHECK(output.contains("ImportDeclaration [0, 27)"));
    CHECK(output.contains("module_reference DomainRoot [7, 18)"));
    CHECK(output.contains("components (2)"));
    CHECK(output.contains("component [7, 11) \"math\""));
    CHECK(output.contains("component [12, 18) \"vector\""));
    CHECK(output.contains("selection WildcardImport [25, 26)"));
    CHECK(!output.contains("quoted"));
}

TEST_CASE("Dump: module and C++ header imports recover source order only for rendering") {
    const auto owned = dump_source(
        "main.cv",
        "import <native/first.hpp>;\n"
        "import .value using Value;\n"
        "import \"native/second.hpp\";"
    );
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    const auto first_header = output.find("cpp_header Angle");
    const auto module_import = output.find("module_reference ParentRelative");
    const auto second_header = output.find("cpp_header Quote");
    REQUIRE_NE(first_header, std::string::npos);
    REQUIRE_NE(module_import, std::string::npos);
    REQUIRE_NE(second_header, std::string::npos);
    CHECK(first_header < module_import);
    CHECK(module_import < second_header);
}

TEST_CASE("Dump: C++ source fragments expose form and payload spans") {
    const auto owned = dump_source("native.cv", "#[cpp] ---\nstatic_assert(true);\n---\n");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("cpp_source_fragments (1)"));
    CHECK(output.contains("CppSourceFragment"));
    CHECK(output.contains("form [0, 35)"));
    CHECK(output.contains("payload [11, 32) \"static_assert(true);\\n\""));
}

TEST_CASE("Dump: declaration visibility and constants remain structured") {
    const auto owned = dump_source("constants.cv", "private const answer: i32 = 42;");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("ConstantDeclaration"));
    CHECK(output.contains("visibility Private"));
    CHECK(output.contains("name [14, 20) \"answer\""));
    CHECK(output.contains("type NamedType"));
    CHECK(output.contains("initializer Literal"));
}

TEST_CASE("Dump: local module references expose their owned fields") {
    const auto owned = dump_source("main.cv", "import .model.user using User;");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("module_reference ParentRelative"));
    CHECK(output.contains("prefix [7, 8) \".\""));
    CHECK(output.contains("component [8, 13) \"model\""));
    CHECK(output.contains("component [14, 18) \"user\""));
}

TEST_CASE("Dump: craft-qualified module references retain their qualifier") {
    const auto owned = dump_source("main.cv", "import json::model.user using User;");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("module_reference CraftQualified"));
    CHECK(output.contains("craft [7, 11) \"json\""));
    CHECK(output.contains("separator [11, 13) \"::\""));
    CHECK(output.contains("component [13, 18) \"model\""));
    CHECK(output.contains("component [19, 23) \"user\""));
}

TEST_CASE("Dump: half-open range iterables expose their ordered syntax") {
    const auto owned = dump_source("main.cv", "fn f() { for i in begin..end() {} }");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    const auto iterable = output.find("iterable HalfOpenRange [18, 30)");
    const auto begin = output.find("begin NameExpression [18, 23) \"begin\"");
    const auto op = output.find("operator [23, 25) \"..\"");
    const auto end = output.find("end CallExpression [25, 30)");
    REQUIRE(iterable != std::string::npos);
    REQUIRE(begin != std::string::npos);
    REQUIRE(op != std::string::npos);
    REQUIRE(end != std::string::npos);
    CHECK(iterable < begin);
    CHECK(begin < op);
    CHECK(op < end);
}

TEST_CASE("Dump: token lexemes and display paths use stable escaping") {
    const auto owned = dump_source("dir\\sample.cv", "\"line\\n\"");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());

    CHECK_EQ(
        render_token_dump(owned.sources, lexical.value),
        R"DUMP(Tokens "dir\\sample.cv"
└─ StringLiteral [0, 8) "\"line\\n\""
)DUMP"
    );
}

TEST_CASE("Dump: empty and invalid token streams remain explicit") {
    const auto empty_owned = dump_source("empty.cv", "");
    const auto empty = lex(empty_owned.sources.view(empty_owned.source_id));
    REQUIRE(empty.diagnostics.empty());
    CHECK_EQ(
        render_token_dump(empty_owned.sources, empty.value),
        R"DUMP(Tokens "empty.cv"
└─ <empty>
)DUMP"
    );

    const auto invalid_owned = dump_source("invalid.cv", "@");
    const auto invalid = lex(invalid_owned.sources.view(invalid_owned.source_id));
    REQUIRE(!invalid.diagnostics.empty());
    CHECK_EQ(
        render_token_dump(invalid_owned.sources, invalid.value),
        R"DUMP(Tokens "invalid.cv"
└─ Invalid [0, 1) "@"
)DUMP"
    );
}

TEST_CASE("Dump: empty AST roots remain explicit") {
    const auto owned = dump_source("empty.cv", "");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    CHECK_EQ(
        render_ast_dump(owned.sources, *parsed),
        R"DUMP(SourceModule [0, 0) "empty.cv"
├─ imports (0)
├─ cpp_source_fragments (0)
└─ items (0)
)DUMP"
    );
}

TEST_CASE("Dump: non-printing display-path bytes use hexadecimal escapes") {
    const auto owned = dump_source("line\n\t\x01.cv", "");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());

    const auto output = render_token_dump(owned.sources, lexical.value);
    CHECK_EQ(output, "Tokens \"line\\n\\t\\x01.cv\"\n└─ <empty>\n");
    CHECK(output.ends_with('\n'));
}

TEST_CASE("Dump: invalid UTF-8 token bytes use hexadecimal escapes") {
    const auto text = std::string("\xff\xc0\x80", 3);
    const auto owned = dump_source("invalid-utf8.cv", text);
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(!lexical.diagnostics.empty());

    CHECK_EQ(
        render_token_dump(owned.sources, lexical.value),
        R"DUMP(Tokens "invalid-utf8.cv"
├─ Invalid [0, 1) "\xff"
├─ Invalid [1, 2) "\xc0"
└─ Invalid [2, 3) "\x80"
)DUMP"
    );
}

TEST_CASE("Dump: repeated rendering is deterministic") {
    const auto owned = dump_source("main.cv", "fn value() -> i32 { return 1; }");
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    CHECK_EQ(
        render_token_dump(owned.sources, lexical.value),
        render_token_dump(owned.sources, lexical.value)
    );
    CHECK_EQ(render_ast_dump(owned.sources, *parsed), render_ast_dump(owned.sources, *parsed));
}

TEST_CASE("Dump: sum type syntax exposes payload, contextual cases, patterns, and guards") {
    const auto owned = dump_source(
        "sum.cv",
        "enum Shape { Circle(f64), Point }\n"
        "fn area(shape) { match shape { .Circle(radius) if radius > 0.0 => radius, "
        "Shape::Point => .Point } }"
    );
    const auto source = owned.sources.view(owned.source_id);
    const auto lexical = lex(source);
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());

    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("payload_types (1)"));
    CHECK(output.contains("CasePattern"));
    CHECK(output.contains("qualifier_components (1)"));
    CHECK(output.contains("guard BinaryExpression"));
    CHECK(output.contains("ContextualCaseExpression"));
}

TEST_CASE("Dump: global C++ names expose the root and complete path") {
    const auto owned = dump_source("main.cv", "fn f(value: ::Point) { ::vendor::call(value); }");
    const auto lexical = lex(owned.sources.view(owned.source_id));
    REQUIRE(lexical.diagnostics.empty());
    const auto parsed = parse(owned.sources, lexical.value);
    REQUIRE(parsed.has_value());
    const auto output = render_ast_dump(owned.sources, *parsed);
    CHECK(output.contains("CppNameExpression"));
    CHECK(output.contains("global_root [12, 14) \"::\""));
    CHECK(output.contains("global_root [23, 25) \"::\""));
    CHECK(output.contains("name [25, 31) \"vendor\""));
}
