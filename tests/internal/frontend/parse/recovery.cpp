module carven:test.internal.frontend.parse.recovery;

import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.lex;
import :frontend.literal;
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
    require_equal(ast.ast_module().items.size(), names.size());
    for (auto index = 0uz; index < names.size(); ++index) {
        const auto id = ast.ast_module().items[index];
        require(id.index() < ast.items().size());
        const auto* function = std::get_if<ASTFunctionDecl>(&ast.item(id).value);
        require(function != nullptr);
        expect_equal(slice(text, function->name_span), names[index]);
    }
}

// Read a recovered body's real typed edges, so a surviving root with dangling
// children or children accidentally retained from the discarded item cannot pass.
auto check_construction(const SyntaxTree& tree, std::string_view text) noexcept -> void {
    const auto ast = tree.view();
    require_equal(ast.ast_module().items.size(), 1uz);
    const auto item_id = ast.ast_module().items.front();
    require(item_id.index() < ast.items().size());
    const auto* function = std::get_if<ASTFunctionDecl>(&ast.item(item_id).value);
    require(function != nullptr);
    const auto* implementation = std::get_if<ASTFunctionBody>(&function->implementation);
    require(implementation != nullptr);
    const auto* block_id = std::get_if<ASTBlockID>(&implementation->body);
    require(block_id != nullptr);
    require(block_id->index() < ast.blocks().size());
    const auto& body = ast.block(*block_id);
    require_equal(body.statements.size(), 1uz);
    const auto statement_id = body.statements.front();
    require(statement_id.index() < ast.statements().size());
    const auto* binding = std::get_if<ASTVariableDecl>(&ast.statement(statement_id).value);
    require(binding != nullptr);
    const auto* named = std::get_if<ASTNamedBindingTarget>(&binding->target);
    require(named != nullptr);
    expect_equal(slice(text, named->name_span), std::string_view("value"));
    require(binding->initializer.has_value());
    require(binding->initializer->index() < ast.expressions().size());
    const auto* construction =
        std::get_if<ASTConstructionExpr>(&ast.expression(*binding->initializer).value);
    require(construction != nullptr);
    const auto* fields = std::get_if<ASTFieldInitializerList>(&construction->initializer.value);
    require(fields != nullptr);
    require_equal(fields->fields.size(), 1uz);
    const auto& field = fields->fields.front();
    expect_equal(slice(text, field.name_span), std::string_view("field"));
    require(field.value.index() < ast.expressions().size());
    const auto& expression = ast.expression(field.value);
    expect_equal(slice(text, expression.span), std::string_view("42"));
    const auto* literal = std::get_if<ASTLiteral>(&expression.value);
    require(literal != nullptr);
    expect(std::holds_alternative<IntegerLiteralValue>(literal->value));
}

const TestSuite tests([] static noexcept {
    "Parser recovery: valid input retains an ordinary complete tree"_test = [] static noexcept {
        static constexpr auto text =
            std::string_view("fn recovered() { let value = Model { field: 42 }; }");
        const auto result = recover_source(text);
        require(result.value.has_value());
        expect(result.diagnostics.empty());
        const auto names = std::array {std::string_view("recovered")};
        check_function_names(*result.value, text, names);
        check_construction(*result.value, text);
        expect(parse_source(text).has_value());

        // Rejected alternatives must not attach diagnostics to accepted syntax.
        static constexpr auto for_text =
            std::string_view("fn f() { for var i = 0; i < 3; i += 1 {} }");
        const auto for_result = recover_source(for_text);
        require(for_result.value.has_value());
        expect(for_result.diagnostics.empty());
        const auto for_names = std::array {std::string_view("f")};
        check_function_names(*for_result.value, for_text, for_names);
        expect(parse_source(for_text).has_value());
    };

    "Parser recovery: diagnostics retain complete declarations on both sides"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn first() { return 1; }\n"
                "fn broken(1) {}\n"
                "fn last() { return 2; }\n"
            );
            const auto result = recover_source(text);
            require(result.value.has_value());
            require_equal(result.diagnostics.size(), 1uz);
            const auto names = std::array {std::string_view("first"), std::string_view("last")};
            check_function_names(*result.value, text, names);
            const auto strict = parse_source(text);
            require(!strict.has_value());
            require_equal(strict.error().size(), result.diagnostics.size());
            expect_equal(
                strict.error().front().finding.code,
                result.diagnostics.front().finding.code
            );
            require(result.diagnostics.front().attachment.primary.has_value());
            require(strict.error().front().attachment.primary.has_value());
            expect_equal(
                strict.error().front().attachment.primary->span.span.start(),
                result.diagnostics.front().attachment.primary->span.span.start()
            );
        };

    "Parser recovery: nested failures rewind child storage and construction state"_test =
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string_view text;
            };
            const auto inputs = std::array {
                Input {
                    .name = "nested construction",
                    .text = "fn broken() { let bad = Model { outer: Nested { x: 1, y } }; }\n"
                            "fn recovered() { let value = Model { field: 42 }; }\n"
                },
                Input {
                    .name = "for step",
                    .text = "fn broken() { for ; ready; value = { } }\n"
                            "fn recovered() { let value = Model { field: 42 }; }\n"
                },
                Input {
                    .name = "cpp import declaration",
                    .text = "import(cpp) fn broken(1);\n"
                            "fn recovered() { let value = Model { field: 42 }; }\n"
                },
            };
            each(inputs, &Input::name, [](const Input& input) static noexcept {
                const auto result = recover_source(input.text);
                require(result.value.has_value());
                require_equal(result.diagnostics.size(), 1uz);
                const auto names = std::array {std::string_view("recovered")};
                check_function_names(*result.value, input.text, names);
                check_construction(*result.value, input.text);
                expect(!parse_source(input.text).has_value());
            });
        };

    "Parser recovery: nested declarations never become module items"_test = [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view discarded;
        };
        const auto inputs = std::array {
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
            require(result.value.has_value());
            expect(has_errors(result));
            const auto names = std::array {std::string_view("recovered")};
            check_function_names(*result.value, text, names);
            check_construction(*result.value, text);
            expect(!parse_source(text).has_value());
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
            require(result.value.has_value());
            expect_diagnostic(result.diagnostics, DiagnosticCode::ParseNestingTooDeep);
            const auto names = std::array {std::string_view("recovered")};
            check_function_names(*result.value, text, names);
            check_construction(*result.value, text);
            expect(!parse_source(text).has_value());
        };

    "Parser recovery: malformed initial imports do not publish a syntax owner"_test =
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string_view text;
            };
            const auto inputs = std::array {
                Input {.name = "module import", .text = "import .helper using ;\nfn intact() {}"},
                Input {.name = "header import", .text = "import <vector> using ;\nfn intact() {}"},
            };
            each(inputs, &Input::name, [](const Input& input) static noexcept {
                const auto result = recover_source(input.text);
                expect(!result.value.has_value());
                expect(!result.diagnostics.empty());
                expect(!parse_source(input.text).has_value());
            });
        };

    "Parser recovery: delimiter preflight remains a fatal boundary"_test = [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view text;
        };
        const auto inputs = std::array {
            Input {.name = "unclosed", .text = "fn intact() {}\nfn broken() {"},
            Input {.name = "mismatched", .text = "fn intact() {}\nfn broken() { let x = [1); }"},
        };
        each(inputs, &Input::name, [](const Input& input) static noexcept {
            const auto result = recover_source(input.text);
            expect(!result.value.has_value());
            require_equal(result.diagnostics.size(), 1uz);
            expect(!parse_source(input.text).has_value());
        });
    };

    "Parser recovery: moving the syntax owner preserves recovered imports and children"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "import .helper using helper;\n"
                "fn broken(1) {}\n"
                "fn recovered() { let value = Model { field: 42 }; }\n"
            );
            auto result = recover_source(text);
            require(result.value.has_value());
            require_equal(result.diagnostics.size(), 1uz);
            const auto ast = result.value->view();
            require_equal(ast.ast_module().module_imports.size(), 1uz);
            require_equal(ast.ast_module().items.size(), 1uz);
            const auto import_id = ast.ast_module().module_imports.front();
            const auto item_id = ast.ast_module().items.front();
            const auto source_id = ast.source_id();
            auto moved = std::move(*result.value);
            result.value.reset();
            const auto moved_ast = moved.view();
            expect(moved_ast.source_id() == source_id);
            require(import_id.index() < moved_ast.module_imports().size());
            require(item_id.index() < moved_ast.items().size());
            const auto* reference = std::get_if<ASTParentRelativeModuleReference>(
                &moved_ast.module_import(import_id).module_reference.value
            );
            require(reference != nullptr);
            require_equal(reference->components.size(), 1uz);
            expect_equal(slice(text, reference->components.front()), std::string_view("helper"));
            const auto names = std::array {std::string_view("recovered")};
            check_function_names(moved, text, names);
            check_construction(moved, text);
        };
});

} // namespace
