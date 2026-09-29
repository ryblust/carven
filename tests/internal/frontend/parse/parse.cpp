module carven:test.internal.frontend.parse.parse;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex;
import :frontend.lex.token;
import :frontend.parse;
import :frontend.parse.builder;
import :source.manager;
import :source.text;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

static_assert(!std::constructible_from<SyntaxTree, ASTStorage, ASTModule, SourceID>);
static_assert(!std::default_initializable<ASTBuilder>);
static_assert(!std::constructible_from<ASTBuilder, SourceView>);

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Parser: empty input owns an empty typed module root", [] static noexcept {
        const auto result = parse_valid("");
        const auto& module_syntax = root(result);
        ct::expect_equal(module_syntax.span.start(), 0u);
        ct::expect_equal(module_syntax.span.end(), 0u);
        ct::expect(module_syntax.module_imports.empty());
        ct::expect(module_syntax.cpp_header_imports.empty());
        ct::expect(module_syntax.items.empty());
    });

    ct::test("Parser: token source identity crosses the API boundary", [] static noexcept {
        auto sources = SourceManager();
        const auto first = *sources.append_virtual("first.cv", "fn first() {}");
        const auto second = *sources.append_virtual("second.cv", "fn second() {}");
        const auto lexical = lex(sources.view(first));
        if (!ct::expect(lexical.diagnostics.empty())) {
            return;
        }
        ct::expect(((lexical.value.source_id()) == (first)))
            .note("lexical.value.source_id() == first");
        ct::expect(lexical.value.source_id() != second)
            .note("lexical source ID differs from second source");

        const auto parsed = parse(sources, lexical.value);
        if (!ct::expect(parsed.has_value())) {
            return;
        }
        ct::expect(((parsed->view().source_id()) == (first)))
            .note("parsed->view().source_id() == first");
        ct::expect_equal(
            slice(sources.view(first).text, function(*parsed).name_span),
            std::string_view("first")
        );
    });

    ct::test("Parser: syntax diagnostics use one structured primary label", [] static noexcept {
        const auto result = parse_source("fn main(1) {}");
        if (!ct::expect(!result.has_value())) {
            return;
        }
        if (!ct::expect_equal(result.error().size(), 1uz)) {
            return;
        }
        ct::expect_equal(
            result.error()[0].finding.message,
            std::string_view("expected parameter name")
        );
        if (!ct::expect(result.error()[0].attachment.primary.has_value())) {
            return;
        }
        ct::expect_equal(result.error()[0].attachment.primary->span.span.start(), 8u);
        ct::expect_equal(result.error()[0].attachment.primary->span.span.end(), 9u);
    });

    ct::test(
        "Parser: delimiter preflight reports mismatched and unclosed delimiters",
        [] static noexcept {
            const auto mismatched = parse_source("fn main() { let value = [1); }");
            if (!ct::expect(!mismatched.has_value())) {
                return;
            }
            if (!ct::expect_equal(mismatched.error().size(), 1uz)) {
                return;
            }
            ct::expect_equal(
                mismatched.error().front().finding.message,
                std::string_view("mismatched closing delimiter ')'; expected ']'")
            );
            if (!ct::expect(mismatched.error().front().attachment.primary.has_value())) {
                return;
            }
            ct::expect_equal(mismatched.error().front().attachment.primary->span.span.start(), 26u);
            ct::expect_equal(mismatched.error().front().attachment.primary->span.span.end(), 27u);

            const auto unclosed = parse_source("fn main() {");
            if (!ct::expect(!unclosed.has_value())) {
                return;
            }
            if (!ct::expect_equal(unclosed.error().size(), 1uz)) {
                return;
            }
            ct::expect_equal(
                unclosed.error().front().finding.message,
                std::string_view("unclosed delimiter '{'; expected '}'")
            );
            if (!ct::expect(unclosed.error().front().attachment.primary.has_value())) {
                return;
            }
            ct::expect_equal(unclosed.error().front().attachment.primary->span.span.start(), 10u);
            ct::expect_equal(unclosed.error().front().attachment.primary->span.span.end(), 11u);
        }
    );

    ct::test(
        "Parser: committed item recovery reports independent declarations",
        [] static noexcept {
            const auto result = parse_source(
                "fn first(1) {}\n"
                "fn second(2) {}\n"
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            if (!ct::expect_equal(result.error().size(), 2uz)) {
                return;
            }
            ct::expect_equal(
                result.error()[0].finding.message,
                std::string_view("expected parameter name")
            );
            ct::expect_equal(
                result.error()[1].finding.message,
                std::string_view("expected parameter name")
            );
        }
    );

    ct::test(
        "Parser: visibility and constant starts participate in item recovery",
        [] static noexcept {
            const auto result = parse_source(
                "const first =;\n"
                "private const second =;\n"
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            if (!ct::expect_equal(result.error().size(), 2uz)) {
                return;
            }
            ct::expect_equal(
                result.error()[0].finding.message,
                std::string_view("expected expression")
            );
            ct::expect_equal(
                result.error()[1].finding.message,
                std::string_view("expected expression")
            );
        }
    );

    ct::test("Parser: later speculation never replaces a committed diagnostic", [] static noexcept {
        const auto result = parse_source(
            "fn bad() -> i32 { return 1 + }\n"
            "fn good() -> i32 { return 2; }\n"
            "test \"probe\" { check(good() == 2); }\n"
        );
        if (!ct::expect(!result.has_value())) {
            return;
        }
        if (!ct::expect_equal(result.error().size(), 1uz)) {
            return;
        }
        ct::expect_equal(
            result.error()[0].finding.message,
            std::string_view("expected expression")
        );
        if (!ct::expect(result.error()[0].attachment.primary.has_value())) {
            return;
        }
        ct::expect_less(result.error()[0].attachment.primary->span.span.start(), 33u);
    });

    ct::test(
        "Parser: a failed C-style for step restores construction boundaries",
        [] static noexcept {
            static constexpr auto source = std::string_view(
                "fn broken() { for ;; value = { } }\n"
                "fn recovered() { let value = Model { field: 1 }; }\n"
            );
            static constexpr auto invalid_step = source.find("value =");
            static constexpr auto recovered_declaration = source.find("fn recovered");
            const auto result = parse_source(source);
            if (!ct::expect(!result.has_value())) {
                return;
            }
            if (!ct::expect_equal(result.error().size(), 1uz)) {
                return;
            }
            ct::expect_equal(result.error().front().finding.code, DiagnosticCode::Syntax);
            if (!ct::expect(result.error().front().attachment.primary.has_value())) {
                return;
            }
            const auto diagnostic_start =
                result.error().front().attachment.primary->span.span.start();
            ct::expect_greater_equal(diagnostic_start, invalid_step);
            ct::expect_less(diagnostic_start, recovered_declaration);
        }
    );

    ct::test(
        "Parser: excessive syntax nesting returns a deterministic diagnostic",
        [] static noexcept {
            auto source = std::string("fn deep() { let value = ");
            source.append(600, '(');
            source += '1';
            source.append(600, ')');
            source += "; }";

            const auto result = parse_source(source);
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect(
                std::ranges::any_of(
                    result.error(),
                    [](const Diagnostic& diagnostic) static noexcept {
                        return diagnostic.finding.code == "CV-PARSE-NESTING-TOO-DEEP";
                    }
                )
            );
        }
    );

    ct::test("Parser: moving SyntaxTree preserves its module and owned IDs", [] static noexcept {
        static constexpr auto text = std::string_view(
            "import .helper using helper;\n"
            "fn answer() -> i32 { return 42; }"
        );
        auto parsed = parse_source(text);
        if (!ct::expect(parsed.has_value())) {
            return;
        }
        const auto import_id = root(*parsed).module_imports.front();
        const auto item_id = root(*parsed).items.front();

        auto moved = std::move(parsed);
        if (!ct::expect(moved.has_value())) {
            return;
        }
        const auto ast = moved->view();
        ct::expect_equal(ast.items().size(), 1uz);
        ct::expect_equal(ast.ast_module().items.size(), 1uz);
        ct::expect_equal(
            slice(
                text,
                std::get<ASTParentRelativeModuleReference>(
                    ast.module_import(import_id).module_reference.value
                )
                    .components.front()
            ),
            std::string_view("helper")
        );
        ct::expect_equal(
            slice(text, get<ASTFunctionDecl>(ast.item(item_id)).name_span),
            std::string_view("answer")
        );
    });

    ct::test("Parser: incomplete construction fields require an initializer", [] static noexcept {
        check_invalid(
            "fn f() { let value = Vec { x: 1, y }; }",
            "expected ':' after initializer field name"
        );
    });

    ct::test(
        "Parser: recovery ignores declaration starts inside a failed item's nested blocks",
        [] static noexcept {
            const auto result = parse_source(
                "fn valid() { if true { let value = 1; } }\n"
                "fn broken() { if true { let x = ; fn nested(1) {} } }\n"
                "fn next(2) {}\n"
                "fn last(3) {}\n"
            );
            if (!ct::expect(!(result.has_value()))) {
                return;
            }
            if (!ct::expect_equal(result.error().size(), 3uz)) {
                return;
            }
            ct::expect_equal(
                result.error()[0].finding.message,
                std::string_view("expected expression")
            );
            ct::expect_equal(
                result.error()[1].finding.message,
                std::string_view("expected parameter name")
            );
            ct::expect_equal(
                result.error()[2].finding.message,
                std::string_view("expected parameter name")
            );
        }
    );
});

} // namespace
