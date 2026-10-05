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

const TestSuite suite([] static noexcept {
    "Parser: empty input owns an empty typed module root"_test = [] static noexcept {
        const auto result = parse_valid("");
        const auto& module_syntax = root(result);
        expect_equal(module_syntax.span.start(), 0u);
        expect_equal(module_syntax.span.end(), 0u);
        expect(module_syntax.module_imports.empty());
        expect(module_syntax.cpp_header_imports.empty());
        expect(module_syntax.items.empty());
    };

    "Parser: token source identity crosses the API boundary"_test = [] static noexcept {
        auto sources = SourceManager();
        const auto first = *sources.append_virtual("first.cv", "fn first() {}");
        const auto second = *sources.append_virtual("second.cv", "fn second() {}");
        const auto lexical = lex(sources.view(first));
        if (!expect(lexical.diagnostics.empty())) {
            return;
        }
        expect(((lexical.value.source_id()) == (first))).note("lexical.value.source_id() == first");
        expect(lexical.value.source_id() != second)
            .note("lexical source ID differs from second source");

        const auto parsed = parse(sources, lexical.value);
        if (!expect(parsed.has_value())) {
            return;
        }
        expect(((parsed->view().source_id()) == (first)))
            .note("parsed->view().source_id() == first");
        expect_equal(
            slice(sources.view(first).text, function(*parsed).name_span),
            std::string_view("first")
        );
    };

    "Parser: syntax diagnostics use one structured primary label"_test = [] static noexcept {
        const auto result = parse_source("fn main(1) {}");
        if (!expect(!result.has_value())) {
            return;
        }
        if (!expect_equal(result.error().size(), 1uz)) {
            return;
        }
        expect_equal(
            result.error()[0].finding.message,
            std::string_view("expected parameter name")
        );
        if (!expect(result.error()[0].attachment.primary.has_value())) {
            return;
        }
        expect_equal(result.error()[0].attachment.primary->span.span.start(), 8u);
        expect_equal(result.error()[0].attachment.primary->span.span.end(), 9u);
    };

    "Parser: delimiter preflight reports mismatched and unclosed delimiters"_test =
        [] static noexcept {
            const auto mismatched = parse_source("fn main() { let value = [1); }");
            if (!expect(!mismatched.has_value())) {
                return;
            }
            if (!expect_equal(mismatched.error().size(), 1uz)) {
                return;
            }
            expect_equal(
                mismatched.error().front().finding.message,
                std::string_view("mismatched closing delimiter ')'; expected ']'")
            );
            if (!expect(mismatched.error().front().attachment.primary.has_value())) {
                return;
            }
            expect_equal(mismatched.error().front().attachment.primary->span.span.start(), 26u);
            expect_equal(mismatched.error().front().attachment.primary->span.span.end(), 27u);

            const auto unclosed = parse_source("fn main() {");
            if (!expect(!unclosed.has_value())) {
                return;
            }
            if (!expect_equal(unclosed.error().size(), 1uz)) {
                return;
            }
            expect_equal(
                unclosed.error().front().finding.message,
                std::string_view("unclosed delimiter '{'; expected '}'")
            );
            if (!expect(unclosed.error().front().attachment.primary.has_value())) {
                return;
            }
            expect_equal(unclosed.error().front().attachment.primary->span.span.start(), 10u);
            expect_equal(unclosed.error().front().attachment.primary->span.span.end(), 11u);
        };

    "Parser: committed item recovery reports independent declarations"_test = [] static noexcept {
        const auto result = parse_source(
            "fn first(1) {}\n"
            "fn second(2) {}\n"
        );
        if (!expect(!result.has_value())) {
            return;
        }
        if (!expect_equal(result.error().size(), 2uz)) {
            return;
        }
        expect_equal(
            result.error()[0].finding.message,
            std::string_view("expected parameter name")
        );
        expect_equal(
            result.error()[1].finding.message,
            std::string_view("expected parameter name")
        );
    };

    "Parser: visibility and constant starts participate in item recovery"_test =
        [] static noexcept {
            const auto result = parse_source(
                "const first =;\n"
                "private const second =;\n"
            );
            if (!expect(!result.has_value())) {
                return;
            }
            if (!expect_equal(result.error().size(), 2uz)) {
                return;
            }
            expect_equal(
                result.error()[0].finding.message,
                std::string_view("expected expression")
            );
            expect_equal(
                result.error()[1].finding.message,
                std::string_view("expected expression")
            );
        };

    "Parser: later speculation never replaces a committed diagnostic"_test = [] static noexcept {
        const auto result = parse_source(
            "fn bad() -> i32 { return 1 + }\n"
            "fn good() -> i32 { return 2; }\n"
            "test \"probe\" { check(good() == 2); }\n"
        );
        if (!expect(!result.has_value())) {
            return;
        }
        if (!expect_equal(result.error().size(), 1uz)) {
            return;
        }
        expect_equal(result.error()[0].finding.message, std::string_view("expected expression"));
        if (!expect(result.error()[0].attachment.primary.has_value())) {
            return;
        }
        expect_less(result.error()[0].attachment.primary->span.span.start(), 33u);
    };

    "Parser: a failed C-style for step restores construction boundaries"_test = [] static noexcept {
        static constexpr auto source = std::string_view(
            "fn broken() { for ; ready; value = { } }\n"
            "fn recovered() { let value = Model { field: 1 }; }\n"
        );
        static constexpr auto invalid_step = source.find("value =");
        static constexpr auto recovered_declaration = source.find("fn recovered");
        const auto result = parse_source(source);
        if (!expect(!result.has_value())) {
            return;
        }
        if (!expect_equal(result.error().size(), 1uz)) {
            return;
        }
        expect_equal(result.error().front().finding.code, DiagnosticCode::Syntax);
        if (!expect(result.error().front().attachment.primary.has_value())) {
            return;
        }
        const auto diagnostic_start = result.error().front().attachment.primary->span.span.start();
        expect_greater_equal(diagnostic_start, invalid_step);
        expect_less(diagnostic_start, recovered_declaration);
    };

    "Parser: excessive syntax nesting returns a deterministic diagnostic"_test =
        [] static noexcept {
            auto source = std::string("fn deep() { let value = ");
            source.append(600, '(');
            source += '1';
            source.append(600, ')');
            source += "; }";

            const auto result = parse_source(source);
            if (!expect(!result.has_value())) {
                return;
            }
            expect(
                std::ranges::any_of(
                    result.error(),
                    [](const Diagnostic& diagnostic) static noexcept {
                        return diagnostic.finding.code == "CV-PARSE-NESTING-TOO-DEEP";
                    }
                )
            );
        };

    "Parser: moving SyntaxTree preserves its module and owned IDs"_test = [] static noexcept {
        static constexpr auto text = std::string_view(
            "import .helper using helper;\n"
            "fn answer() -> i32 { return 42; }"
        );
        auto parsed = parse_source(text);
        if (!expect(parsed.has_value())) {
            return;
        }
        const auto import_id = root(*parsed).module_imports.front();
        const auto item_id = root(*parsed).items.front();

        auto moved = std::move(parsed);
        if (!expect(moved.has_value())) {
            return;
        }
        const auto ast = moved->view();
        expect_equal(ast.items().size(), 1uz);
        expect_equal(ast.ast_module().items.size(), 1uz);
        expect_equal(
            slice(
                text,
                std::get<ASTParentRelativeModuleReference>(
                    ast.module_import(import_id).module_reference.value
                )
                    .components.front()
            ),
            std::string_view("helper")
        );
        expect_equal(
            slice(text, get<ASTFunctionDecl>(ast.item(item_id)).name_span),
            std::string_view("answer")
        );
    };

    "Parser: incomplete construction fields require an initializer"_test = [] static noexcept {
        check_invalid(
            "fn f() { let value = Vec { x: 1, y }; }",
            "expected ':' after initializer field name"
        );
    };

    "Parser: recovery ignores declaration starts inside a failed item's nested blocks"_test =
        [] static noexcept {
            const auto result = parse_source(
                "fn valid() { if true { let value = 1; } }\n"
                "fn broken() { if true { let x = ; fn nested(1) {} } }\n"
                "fn next(2) {}\n"
                "fn last(3) {}\n"
            );
            if (!expect(!(result.has_value()))) {
                return;
            }
            if (!expect_equal(result.error().size(), 3uz)) {
                return;
            }
            expect_equal(
                result.error()[0].finding.message,
                std::string_view("expected expression")
            );
            expect_equal(
                result.error()[1].finding.message,
                std::string_view("expected parameter name")
            );
            expect_equal(
                result.error()[2].finding.message,
                std::string_view("expected parameter name")
            );
        };
});

} // namespace
