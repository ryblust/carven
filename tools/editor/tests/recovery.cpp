module carven:test.editor.recovery;

import :diagnostics.code;
import :editor.analysis;
import :editor.document;
import :editor.semantic;
import :editor.symbols;
import :frontend.ast.storage;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

auto update(
    editor::AnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    ct::require(host.update(std::string(document), version, std::string(text)).has_value());
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "Editor analysis: recovered syntax retains declarations before and after broken items",
        [] static noexcept {
            const auto broken_items = std::to_array<std::string_view>({
                "fn broken() { let value = ; }",
                "const broken = ;",
                "enum Broken { First(i32), Second(,) }",
            });
            ct::each(
                broken_items,
                [](std::string_view text) static noexcept { return text; },
                [](std::string_view broken_item) static noexcept {
                    auto host = editor::AnalysisHost();
                    const auto text =
                        std::format("fn before() {{}} {} fn after() {{}}", broken_item);
                    update(host, "recovery.cv", 1, text);
                    const auto snapshot = host.snapshot();
                    const auto query = snapshot.document_symbols("recovery.cv");
                    if (!ct::expect(query && query->result)) {
                        return;
                    }
                    ct::expect(!query->document.result->syntax());
                    ct::expect(query->document.result->recovered_syntax().has_value());
                    ct::expect_diagnostic(
                        query->document.result->diagnostics(),
                        DiagnosticCode::Syntax
                    );
                    if (!ct::expect_equal(query->result->size(), 2uz)) {
                        return;
                    }
                    ct::expect_equal((*query->result)[0].name, "before");
                    ct::expect_equal((*query->result)[1].name, "after");
                    for (const auto& symbol : *query->result) {
                        ct::expect_equal(
                            slice(query->document.result->source().text, symbol.selection),
                            symbol.name
                        );
                    }
                    const auto workspace = snapshot.workspace_symbols();
                    if (!ct::expect_equal(workspace->size(), 2uz)) {
                        return;
                    }
                    ct::expect_equal((*workspace)[0].name, "before");
                    ct::expect_equal((*workspace)[1].name, "after");
                }
            );
        }
    );

    ct::test("Editor analysis: discarded lexical scopes contribute no symbols", [] static noexcept {
        auto host = editor::AnalysisHost();
        const auto text = std::string_view(
            "fn before() {}\n"
            "class Broken { value: i32, fn member() -> i32 { return 1; } bad: , }\n"
            R"cv(const broken = ; f"{fn nested() {}}";)cv"
            "\nfn after() {}\n"
        );
        update(host, "record.cv", 1, text);
        const auto snapshot = host.snapshot();
        const auto query = snapshot.document_symbols("record.cv");
        if (!ct::expect(query && query->result)) {
            return;
        }
        const auto recovered = query->document.result->recovered_syntax();
        if (!ct::expect(recovered.has_value())) {
            return;
        }
        ct::expect(!query->document.result->syntax());
        ct::expect_equal(recovered->items().size(), 2uz);
        if (!ct::expect_equal(query->result->size(), 2uz)) {
            return;
        }
        ct::expect_equal((*query->result)[0].name, "before");
        ct::expect_equal((*query->result)[1].name, "after");
        for (const auto& symbol : *query->result) {
            ct::expect(symbol.children.empty());
        }
        const auto workspace = snapshot.workspace_symbols();
        if (!ct::expect_equal(workspace->size(), 2uz)) {
            return;
        }
        ct::expect_equal((*workspace)[0].name, "before");
        ct::expect_equal((*workspace)[1].name, "after");
    });

    ct::test(
        "Editor analysis: empty recovery differs from fatal unavailable syntax",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "empty.cv", 1, "const broken = ;");
            update(host, "valid.cv", 1, "fn good() {}");
            const auto snapshot = host.snapshot();
            const auto empty = snapshot.document_symbols("empty.cv");
            const auto valid = snapshot.document_symbols("valid.cv");
            if (!ct::expect(empty && empty->result && valid && valid->result)) {
                return;
            }
            ct::expect(empty->result->empty());
            ct::expect(!empty->document.result->syntax());
            ct::expect(empty->document.result->recovered_syntax().has_value());
            ct::expect(valid->document.result->syntax().has_value());
            ct::expect(valid->document.result->recovered_syntax().has_value());
            struct FatalScenario final {
                std::string_view name;
                std::string_view text;
                DiagnosticCode code;
            };
            const auto fatal = std::array {
                FatalScenario {
                    .name = "lexical",
                    .text = "@ fn good() {}",
                    .code = DiagnosticCode::Lexical
                },
                FatalScenario {
                    .name = "preflight",
                    .text = "fn good() {} )",
                    .code = DiagnosticCode::Syntax
                },
                FatalScenario {
                    .name = "initial import",
                    .text = "import ; fn good() {}",
                    .code = DiagnosticCode::Syntax
                },
            };
            ct::each(
                fatal,
                &FatalScenario::name,
                [](const FatalScenario& scenario) static noexcept {
                    auto fatal_host = editor::AnalysisHost();
                    update(fatal_host, "fatal.cv", 1, scenario.text);
                    const auto fatal_query = fatal_host.snapshot().document_symbols("fatal.cv");
                    if (!ct::expect(fatal_query.has_value())) {
                        return;
                    }
                    ct::expect(!fatal_query->result);
                    ct::expect(!fatal_query->document.result->syntax());
                    ct::expect(!fatal_query->document.result->recovered_syntax());
                    ct::expect_diagnostic(
                        fatal_query->document.result->diagnostics(),
                        scenario.code
                    );
                    ct::expect(fatal_host.snapshot().workspace_symbols()->empty());
                }
            );
        }
    );

    ct::test(
        "Editor analysis: repaired snapshots preserve recovered source locations and owners",
        [] static noexcept {
            const auto retained = []() static noexcept {
                auto host = editor::AnalysisHost();
                const auto original_text =
                    std::string_view("// old\nfn before() {} const broken = ; fn after() {}");
                update(host, "retained.cv", 1, original_text);
                const auto before = host.snapshot();
                const auto old = before.document_symbols("retained.cv");
                const auto old_workspace = before.workspace_symbols();
                ct::require(old && old->result);
                update(host, "retained.cv", 2, "fn before() {} const repaired = 1; fn after() {}");
                const auto after = host.snapshot();
                const auto repaired = after.document_symbols("retained.cv");
                ct::require(repaired && repaired->result);
                ct::expect(repaired->document.result->syntax().has_value());
                ct::expect(repaired->document.result->diagnostics().empty());
                ct::expect_equal(repaired->result->size(), 3uz);
                ct::expect_equal(old->result->size(), 2uz);
                ct::expect(before.workspace_symbols() == old_workspace);
                ct::expect_equal(after.workspace_symbols()->size(), 3uz);
                ct::expect_equal(old->document.version, 1ll);
                ct::expect_equal(repaired->document.version, 2ll);
                ct::expect_equal(old->document.result->source().text, original_text);
                return *old;
            }();
            ct::expect(retained.document.result->recovered_syntax().has_value());
            ct::expect(!retained.document.result->syntax());
            for (const auto& symbol : *retained.result) {
                ct::expect_equal(
                    slice(retained.document.result->source().text, symbol.selection),
                    symbol.name
                );
            }
            const auto* diagnostic = ct::find_diagnostic(
                retained.document.result->diagnostics(),
                DiagnosticCode::Syntax
            );
            if (!ct::expect(diagnostic && diagnostic->attachment.primary)) {
                return;
            }
            ct::expect_equal(
                retained.document.result->sources()
                    .view(diagnostic->attachment.primary->span.source_id)
                    .origin,
                "retained.cv"
            );
        }
    );

    ct::test(
        "Editor analysis: equal recovered symbols stop index invalidation without publishing semantics",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            const auto first_text =
                std::string_view("fn good() -> i32 { return 1; } const broken = ; fn after() {}");
            const auto second_text =
                std::string_view("fn good() -> i32 { return 1; } const otherx = ; fn after() {}");
            update(host, "a.cv", 1, first_text);
            const auto before = host.snapshot();
            const auto original = before.document_symbols("a.cv");
            const auto original_index = before.workspace_symbols();
            ct::require(original && original->result);
            update(host, "a.cv", 2, second_text);
            const auto after = host.snapshot();
            const auto current = after.document_symbols("a.cv");
            ct::require(current && current->result);
            ct::expect(current->document.result != original->document.result);
            ct::expect(current->result == original->result);
            ct::expect(after.workspace_symbols() == original_index);
            ct::expect_equal(current->document.result->source().text, second_text);
            ct::expect_equal(original->document.result->source().text, first_text);
            ct::expect_equal(after.counts().syntax, 2uz);
            ct::expect_equal(after.counts().document_symbols, 2uz);
            ct::expect_equal(after.counts().workspace_symbols, 1uz);
            auto module_path = CanonicalModulePath::from_value("main");
            ct::require(module_path.has_value());
            const auto project = std::array {
                editor::ProjectModule {.document = "a.cv", .module_path = std::move(*module_path)}
            };
            const auto semantic = after.semantic(project);
            ct::expect(semantic.result->program() == nullptr);
            ct::expect_diagnostic(semantic.result->diagnostics(), DiagnosticCode::Syntax);
            const auto hover =
                after.hover(project, "a.cv", static_cast<std::uint32_t>(second_text.find("1")));
            const auto definition = after.definition(
                project,
                "a.cv",
                static_cast<std::uint32_t>(second_text.find("good"))
            );
            ct::expect(!hover.result);
            ct::expect(!definition.result);
            ct::expect(hover.analysis.result == semantic.result);
            ct::expect(definition.analysis.result == semantic.result);
            ct::expect_equal(after.counts().semantic, 1uz);
        }
    );
});

} // namespace
