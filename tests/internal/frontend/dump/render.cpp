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

    ct::test("Syntax dump: AST output identifies the source and declaration", [] static noexcept {
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
        ct::expect(output.contains("FunctionDeclaration [0, 12)"));
        ct::expect(output.contains("name [3, 7) \"main\""));
        ct::expect(output.contains("body OrdinaryBlock [10, 12)"));
        ct::expect(!output.contains('\x1b'));
        ct::expect(!output.contains("0x"));
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
});

} // namespace
