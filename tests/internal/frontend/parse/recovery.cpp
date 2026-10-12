module carven:test.internal.frontend.parse.recovery;

import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.lex;
import :frontend.parse;
import :source.manager;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

auto recover_source(std::string_view text) noexcept -> Diagnosed<std::optional<SyntaxTree>> {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("recovery-test.cv", std::string(text));
    require(source.has_value());
    const auto lexical = lex(sources.view(*source));
    require(lexical.diagnostics.empty()).note("source = ", text);
    return parse_recovering(sources, lexical.value);
}

auto check_function_names(
    const SyntaxTree& tree,
    std::string_view text,
    std::span<const std::string_view> names
) noexcept -> void {
    const auto ast = tree.view();
    if (!expect_equal(ast.ast_module().items.size(), names.size())) {
        return;
    }
    for (auto index = 0uz; index < names.size(); ++index) {
        const auto id = ast.ast_module().items[index];
        const auto* function = std::get_if<ASTFunctionDecl>(&ast.item(id).value);
        if (!expect(function != nullptr)) {
            continue;
        }
        expect_equal(slice(text, function->name_span), names[index]);
    }
}

const TestSuite tests([] static noexcept {
    "Parser recovery: diagnostics retain complete declarations on both sides"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn first() { return 1; }\n"
                "fn broken() { let bad = Model { outer: Nested { x: 1, y } }; }\n"
                "fn last() { return 2; }\n"
            );
            const auto result = recover_source(text);
            if (!expect(result.value.has_value())) {
                return;
            }
            constexpr auto names = std::array {std::string_view("first"), std::string_view("last")};
            check_function_names(*result.value, text, names);
            const auto strict = parse_source(text);
            if (!expect(!strict.has_value())
                || !expect_equal(strict.error().size(), 1uz)
                || !expect_equal(result.diagnostics.size(), 1uz)) {
                return;
            }
            expect_equal(
                strict.error().front().finding.code,
                result.diagnostics.front().finding.code
            );
            if (!expect(result.diagnostics.front().attachment.primary.has_value())
                || !expect(strict.error().front().attachment.primary.has_value())) {
                return;
            }
            expect_equal(
                strict.error().front().attachment.primary->span.span.start(),
                result.diagnostics.front().attachment.primary->span.span.start()
            );
        };

    "Parser recovery: nested declarations never become module items"_test = [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view discarded;
        };
        constexpr auto inputs = std::array {
            Input {
                .name = "block",
                .discarded = "fn broken() { if true { let bad = ; fn nested(1) {} } }"
            },
            Input {.name = "parentheses", .discarded = "const broken = ; (fn nested() {});"},
            Input {.name = "brackets", .discarded = "const broken = ; [fn nested() {}];"},
            Input {
                .name = "interpolation",
                .discarded = R"cv(const broken = ; f"{fn nested() {}}";)cv"
            },
            Input {
                .name = "mixed nesting",
                .discarded = R"cv(const broken = ; [f"{(fn nested() {})}"];)cv"
            },
        };
        each(inputs, &Input::name, [](const Input& input) static noexcept {
            const auto text = std::format(
                "{}\nfn recovered() {{ let value = Model {{ field: 42 }}; }}",
                input.discarded
            );
            const auto result = recover_source(text);
            if (!expect(result.value.has_value())) {
                return;
            }
            expect(has_errors(result));
            constexpr auto names = std::array {std::string_view("recovered")};
            check_function_names(*result.value, text, names);
        });
    };

    "Parser recovery: per-item syntax depth failure preserves independent declarations"_test =
        [] static noexcept {
            auto text = std::string("fn broken(value: ");
            for (auto depth = 0uz; depth < 513uz; ++depth) {
                text += "ptr<";
            }
            text += "i32";
            text.append(513uz, '>');
            text += ") {} fn recovered() { let value = Model { field: 42 }; }";
            const auto result = recover_source(text);
            if (!expect(result.value.has_value())) {
                return;
            }
            expect_diagnostic(result.diagnostics, DiagnosticCode::ParseNestingTooDeep);
            constexpr auto names = std::array {std::string_view("recovered")};
            check_function_names(*result.value, text, names);
        };

    "Parser recovery: malformed initial imports do not publish a syntax owner"_test =
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string_view text;
            };
            constexpr auto inputs = std::array {
                Input {.name = "module import", .text = "import .helper using ;\nfn intact() {}"},
                Input {.name = "header import", .text = "import <vector> using ;\nfn intact() {}"},
            };
            each(inputs, &Input::name, [](const Input& input) static noexcept {
                const auto result = recover_source(input.text);
                expect(!result.value.has_value());
                expect(!result.diagnostics.empty());
            });
        };

    "Parser recovery: unmatched delimiters prevent a recovered tree"_test = [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view text;
        };
        constexpr auto inputs = std::array {
            Input {.name = "unclosed", .text = "fn intact() {}\nfn broken() {"},
            Input {.name = "mismatched", .text = "fn intact() {}\nfn broken() { let x = [1); }"},
        };
        each(inputs, &Input::name, [](const Input& input) static noexcept {
            const auto result = recover_source(input.text);
            expect(!result.value.has_value());
            expect_equal(result.diagnostics.size(), 1uz);
        });
    };
});

} // namespace
