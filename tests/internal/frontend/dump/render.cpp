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
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Syntax dump: token output exposes ordered tokens and ranges", [] static noexcept {
        const auto owned = dump_source("main.cv", "fn main() {}");
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }

        const auto output = render_token_dump(owned.sources, lexical.value);
        const auto fn = output.find("Fn [0, 2) \"fn\"");
        const auto name = output.find("Identifier [3, 7) \"main\"");
        const auto body = output.find("RightBrace [11, 12) \"}\"");
        if (!ct::expect(fn != std::string::npos)) {
            return;
        }
        if (!ct::expect(name != std::string::npos)) {
            return;
        }
        if (!ct::expect(body != std::string::npos)) {
            return;
        }
        ct::expect(fn < name);
        ct::expect(name < body);
        ct::expect(!output.contains('\x1b'));
    });

    ct::test(
        "Syntax dump: AST output exposes grammar fields without implementation details",
        [] static noexcept {
            const auto owned = dump_source("main.cv", "fn main() {}");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            ct::expect(output.contains("SourceModule [0, 12) \"main.cv\""));
            ct::expect(output.contains("imports (0)"));
            ct::expect(output.contains("items (1)"));
            ct::expect(output.contains("FunctionDeclaration [0, 12)"));
            ct::expect(output.contains("name [3, 7) \"main\""));
            ct::expect(output.contains("parameters (0)"));
            ct::expect(output.contains("body OrdinaryBlock [10, 12)"));
            ct::expect(output.contains("statements (0)"));
            ct::expect(!output.contains('\x1b'));
            ct::expect(!output.contains("0x"));
        }
    );

    ct::test("Syntax dump: imports expose structured module references", [] static noexcept {
        const auto owned = dump_source("main.cv", "import math.vector using *;");
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        const auto parsed = parse(owned.sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }

        const auto output = render_ast_dump(owned.sources, *parsed);
        ct::expect(output.contains("imports (1)"));
        ct::expect(output.contains("ImportDeclaration [0, 27)"));
        ct::expect(output.contains("module_reference DomainRoot [7, 18)"));
        ct::expect(output.contains("components (2)"));
        ct::expect(output.contains("component [7, 11) \"math\""));
        ct::expect(output.contains("component [12, 18) \"vector\""));
        ct::expect(output.contains("selection WildcardImport [25, 26)"));
        ct::expect(!output.contains("quoted"));
    });

    ct::test(
        "Syntax dump: module and C++ header imports recover source order only for rendering",
        [] static noexcept {
            const auto owned = dump_source(
                "main.cv",
                "import <native/first.hpp>;\n"
                "import .value using Value;\n"
                "import \"native/second.hpp\";"
            );
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            const auto first_header = output.find("cpp_header Angle");
            const auto module_import = output.find("module_reference ParentRelative");
            const auto second_header = output.find("cpp_header Quote");
            if (!ct::expect_not_equal(first_header, std::string::npos)) {
                return;
            }
            if (!ct::expect_not_equal(module_import, std::string::npos)) {
                return;
            }
            if (!ct::expect_not_equal(second_header, std::string::npos)) {
                return;
            }
            ct::expect(first_header < module_import);
            ct::expect(module_import < second_header);
        }
    );

    ct::test("Syntax dump: C++ source fragments expose form and payload spans", [] static noexcept {
        const auto owned = dump_source("native.cv", "#[cpp] ---\nstatic_assert(true);\n---\n");
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        const auto parsed = parse(owned.sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }

        const auto output = render_ast_dump(owned.sources, *parsed);
        ct::expect(output.contains("cpp_source_fragments (1)"));
        ct::expect(output.contains("CppSourceFragment"));
        ct::expect(output.contains("form [0, 35)"));
        ct::expect(output.contains("payload [11, 32) \"static_assert(true);\\n\""));
    });

    ct::test(
        "Syntax dump: declaration visibility and constants remain structured",
        [] static noexcept {
            const auto owned = dump_source("constants.cv", "private const answer: i32 = 42;");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            ct::expect(output.contains("ConstantDeclaration"));
            ct::expect(output.contains("visibility Private"));
            ct::expect(output.contains("name [14, 20) \"answer\""));
            ct::expect(output.contains("type NamedType"));
            ct::expect(output.contains("initializer Literal"));
        }
    );

    ct::test(
        "Syntax dump: test and const block labels distinguish absent and empty",
        [] static noexcept {
            const auto owned =
                dump_source("labels.cv", "test {} test \"\" {} const \"scope\" { const {} }");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            ct::expect(output.contains("TestDeclaration"));
            ct::expect(output.contains("ConstBlock"));
            ct::expect(output.contains("label <absent>"));
            ct::expect(output.contains("label [13, 15) \"\\\"\\\"\""));
            ct::expect(output.contains("label [25, 32) \"\\\"scope\\\"\""));
        }
    );

    ct::test("Syntax dump: local module references expose their owned fields", [] static noexcept {
        const auto owned = dump_source("main.cv", "import .model.user using User;");
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        const auto parsed = parse(owned.sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }

        const auto output = render_ast_dump(owned.sources, *parsed);
        ct::expect(output.contains("module_reference ParentRelative"));
        ct::expect(output.contains("prefix [7, 8) \".\""));
        ct::expect(output.contains("component [8, 13) \"model\""));
        ct::expect(output.contains("component [14, 18) \"user\""));
    });

    ct::test(
        "Syntax dump: craft-qualified module references retain their qualifier",
        [] static noexcept {
            const auto owned = dump_source("main.cv", "import json::model.user using User;");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            ct::expect(output.contains("module_reference CraftQualified"));
            ct::expect(output.contains("craft [7, 11) \"json\""));
            ct::expect(output.contains("separator [11, 13) \"::\""));
            ct::expect(output.contains("component [13, 18) \"model\""));
            ct::expect(output.contains("component [19, 23) \"user\""));
        }
    );

    ct::test(
        "Syntax dump: half-open range iterables expose their ordered syntax",
        [] static noexcept {
            const auto owned = dump_source("main.cv", "fn f() { for i in begin..end() {} }");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            const auto iterable = output.find("iterable RangeExpression [18, 30)");
            const auto begin = output.find("begin NameExpression [18, 23) \"begin\"");
            const auto op = output.find("operator [23, 25) \"..\"");
            const auto end = output.find("end CallExpression [25, 30)");
            if (!ct::expect(iterable != std::string::npos)) {
                return;
            }
            if (!ct::expect(begin != std::string::npos)) {
                return;
            }
            if (!ct::expect(op != std::string::npos)) {
                return;
            }
            if (!ct::expect(end != std::string::npos)) {
                return;
            }
            ct::expect(iterable < begin);
            ct::expect(begin < op);
            ct::expect(op < end);
        }
    );

    ct::test(
        "Syntax dump: token lexemes and display paths use stable escaping",
        [] static noexcept {
            const auto owned = dump_source("dir\\sample.cv", "\"line\\n\"");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }

            ct::expect_equal(
                render_token_dump(owned.sources, lexical.value),
                std::string_view(R"DUMP(Tokens "dir\\sample.cv"
└─ StringLiteral [0, 8) "\"line\\n\""
)DUMP")
            );
        }
    );

    ct::test("Syntax dump: empty and invalid token streams remain explicit", [] static noexcept {
        const auto empty_owned = dump_source("empty.cv", "");
        const auto empty = lex(empty_owned.sources.view(empty_owned.source_id));
        if (!ct::expect(empty.diagnostics.empty())) {
            return;
        }
        ct::expect_equal(
            render_token_dump(empty_owned.sources, empty.value),
            std::string_view(R"DUMP(Tokens "empty.cv"
└─ <empty>
)DUMP")
        );

        const auto invalid_owned = dump_source("invalid.cv", "@");
        const auto invalid = lex(invalid_owned.sources.view(invalid_owned.source_id));
        if (!ct::expect(!invalid.diagnostics.empty())) {
            return;
        }
        ct::expect_equal(
            render_token_dump(invalid_owned.sources, invalid.value),
            std::string_view(R"DUMP(Tokens "invalid.cv"
└─ Invalid [0, 1) "@"
)DUMP")
        );
    });

    ct::test("Syntax dump: empty AST roots remain explicit", [] static noexcept {
        const auto owned = dump_source("empty.cv", "");
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        const auto parsed = parse(owned.sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }

        ct::expect_equal(
            render_ast_dump(owned.sources, *parsed),
            std::string_view(R"DUMP(SourceModule [0, 0) "empty.cv"
├─ imports (0)
├─ cpp_source_fragments (0)
└─ items (0)
)DUMP")
        );
    });

    ct::test(
        "Syntax dump: non-printing display-path bytes use hexadecimal escapes",
        [] static noexcept {
            const auto owned = dump_source("line\n\t\x01.cv", "");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }

            const auto output = render_token_dump(owned.sources, lexical.value);
            ct::expect_equal(
                output,
                std::string_view("Tokens \"line\\n\\t\\x01.cv\"\n└─ <empty>\n")
            );
        }
    );

    ct::test("Syntax dump: invalid UTF-8 token bytes use hexadecimal escapes", [] static noexcept {
        const auto text = std::string("\xff\xc0\x80", 3);
        const auto owned = dump_source("invalid-utf8.cv", text);
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(!lexical.diagnostics.empty())) {
            return;
        }

        ct::expect_equal(
            render_token_dump(owned.sources, lexical.value),
            std::string_view(R"DUMP(Tokens "invalid-utf8.cv"
├─ Invalid [0, 1) "\xff"
├─ Invalid [1, 2) "\xc0"
└─ Invalid [2, 3) "\x80"
)DUMP")
        );
    });

    ct::test(
        "Syntax dump: sum type syntax exposes payload, contextual cases, patterns, and guards",
        [] static noexcept {
            const auto owned = dump_source(
                "sum.cv",
                "enum Shape { Circle(f64), Point }\n"
                "fn area(shape) { match shape { .Circle(radius) if radius > 0.0 => radius, "
                "Shape::Point => .Point } }"
            );
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            ct::expect(output.contains("payload_types (1)"));
            ct::expect(output.contains("CasePattern"));
            ct::expect(output.contains("qualifier_components (1)"));
            ct::expect(output.contains("guard BinaryExpression"));
            ct::expect(output.contains("ContextualCaseExpression"));
        }
    );

    ct::test("Syntax dump: global C++ names expose the root and complete path", [] static noexcept {
        const auto owned =
            dump_source("main.cv", "fn f(value: ::Point) { ::vendor::call(value); }");
        const auto lexical = lex(owned.sources.view(owned.source_id));
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        const auto parsed = parse(owned.sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }
        const auto output = render_ast_dump(owned.sources, *parsed);
        ct::expect(output.contains("CppNameExpression"));
        ct::expect(output.contains("global_root [12, 14) \"::\""));
        ct::expect(output.contains("global_root [23, 25) \"::\""));
        ct::expect(output.contains("name [25, 31) \"vendor\""));
    });
    ct::test(
        "Dump: static function parameter qualifier retains its source span",
        [] static noexcept {
            const auto owned = dump_source("static.cv", "fn pick(const index: usize) => index;");
            const auto source = owned.sources.view(owned.source_id);
            const auto lexical = lex(source);
            if (!ct::expect(lexical.diagnostics.empty())) {
                return;
            }
            const auto parsed = parse(owned.sources, lexical.value);
            if (!ct::expect(parsed.has_value())) {
                return;
            }

            const auto output = render_ast_dump(owned.sources, *parsed);
            ct::expect(output.contains("FunctionParameter [8, 26)"));
            ct::expect(output.contains("const [8, 13) \"const\""));
            ct::expect(output.contains("name [14, 19) \"index\""));
        }
    );

    ct::test("Dump: static control qualifiers retain their source spans", [] static noexcept {
        const auto owned = dump_source(
            "static.cv",
            "fn f(n) { const for i in 0..n { const if i == 0 { continue; } } }"
        );
        const auto source = owned.sources.view(owned.source_id);
        const auto lexical = lex(source);
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        const auto parsed = parse(owned.sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }

        const auto output = render_ast_dump(owned.sources, *parsed);
        ct::expect(output.contains("ForStatement [10, 63)"));
        ct::expect(output.contains("const [10, 15) \"const\""));
        ct::expect(output.contains("for [16, 19) \"for\""));
        ct::expect(output.contains("IfForm [32, 61)"));
        ct::expect(output.contains("const [32, 37) \"const\""));
        ct::expect(output.contains("if [38, 40) \"if\""));
    });
});

} // namespace
