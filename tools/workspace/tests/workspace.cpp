module carven:test.workspace;

import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.document;
import :workspace.semantic;
import :workspace.symbols;
import std;

namespace {

auto project_module(std::string document, std::string_view path) noexcept
    -> WorkspaceProjectModule {
    auto canonical = CanonicalModulePath::from_value(path);
    require(canonical.has_value());
    return {.document = std::move(document), .module_path = std::move(*canonical)};
}

auto update(
    WorkspaceAnalysisHost& host,
    std::string document,
    std::int64_t version,
    std::string text
) noexcept -> void {
    require(host.update(std::move(document), version, std::move(text)).has_value());
}

auto offset(std::string_view text, std::string_view needle) noexcept -> std::uint32_t {
    const auto found = text.find(needle);
    require(found != std::string_view::npos);
    return static_cast<std::uint32_t>(found);
}

auto expect_type(const WorkspaceHoverQuery& query, BuiltinType type) noexcept -> void {
    require(query.analysis.result && query.result);
    const auto* program = query.analysis.result->program();
    require(program != nullptr);
    const auto* published = std::get_if<TypeID>(&query.result->type);
    expect(published != nullptr && *published == program->types().builtin_type(type));
}

const TestSuite tests([] static noexcept {
    "Workspace: editing dependencies updates navigation while retained results stay valid"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(1);
            const auto project = std::array {
                project_module("lib.cv", "pkg.lib"),
                project_module("app.cv", "pkg.app"),
                project_module("other.cv", "other"),
            };
            constexpr auto app = std::string_view(
                "import .lib using answer; fn read() { let result = answer(); return result; }"
            );
            update(host, "lib.cv", 1, "export fn answer() -> i32 => 1;");
            update(host, "app.cv", 1, std::string(app));
            update(host, "other.cv", 1, "fn unfinished(");
            const auto before = host.snapshot();
            const auto old_hover = before.hover(project, "app.cv", offset(app, "result"));
            expect_type(old_hover, BuiltinType::I32);
            const auto old_definition =
                before.definition(project, "app.cv", offset(app, "answer()"));
            require(old_definition.result.has_value());
            expect_equal(old_definition.result->document, "lib.cv");
            expect_equal(old_definition.result->version, 1ll);
            const auto original = old_definition.analysis.result->source("lib.cv");
            require(original.has_value());
            expect_equal(slice(original->text, old_definition.result->range), "answer");
            // Navigation excludes an unrelated unfinished document; a project check includes it.
            const auto failed = before.semantic(project);
            expect(failed.result->program() == nullptr);
            expect(!failed.result->diagnostics().empty());

            update(host, "lib.cv", 2, "export fn answer() -> i64 => 2;");
            update(host, "other.cv", 2, "import pkg.lib using answer; fn other() => answer();");
            const auto after = host.snapshot();
            expect_type(after.hover(project, "app.cv", offset(app, "result")), BuiltinType::I64);
            const auto references = after.references(project, "app.cv", offset(app, "answer()"));
            require(references.result.has_value());
            auto locations = std::vector<std::string>();
            for (const auto& location : *references.result) {
                locations.push_back(location.document);
                const auto source = references.analysis.result->source(location.document);
                require(source.has_value());
                expect_equal(slice(source->text, location.range), "answer");
                expect_equal(location.version, location.document == "app.cv" ? 1ll : 2ll);
            }
            const auto expected = std::vector<std::string> {"app.cv", "lib.cv", "other.cv"};
            expect(locations == expected);
            expect_type(old_hover, BuiltinType::I32);
            expect_type(before.hover(project, "app.cv", offset(app, "result")), BuiltinType::I32);
            expect(!host.update("lib.cv", 1, "stale edit").has_value());
            require(host.remove("lib.cv"));
            const auto missing =
                host.snapshot().definition(project, "app.cv", offset(app, "answer()"));
            expect(!missing.result);
            expect(!missing.analysis.result->diagnostics().empty());
            expect_equal(slice(original->text, old_definition.result->range), "answer");
        };

    "Workspace: source outlines recover during editing and survive document closure"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            update(
                host,
                "untitled:buffer",
                1,
                "// 中文\nstruct Pair { value: i32 } const broken = ; fn after() {}"
            );
            const auto old = host.snapshot().document_symbols("untitled:buffer");
            require(old && old->result);
            expect(!old->document.result->diagnostics().empty());
            require_equal(old->result->size(), 2uz);
            const auto& structure = old->result->front();
            expect_equal(structure.name, "Pair");
            require_equal(structure.children.size(), 1uz);
            expect_equal(
                slice(old->document.result->source().text, structure.children.front().selection),
                "value"
            );
            expect_equal(old->result->back().name, "after");

            update(
                host,
                "untitled:buffer",
                2,
                "// 中文\nstruct Pair { value: i32 } const repaired = 1; fn after() {}"
            );
            const auto repaired = host.snapshot().document_symbols("untitled:buffer");
            require(repaired && repaired->result);
            expect(repaired->document.result->diagnostics().empty());
            expect_equal(repaired->document.version, 2ll);
            expect_equal(repaired->result->size(), 3uz);
            require(host.remove("untitled:buffer"));
            expect(host.snapshot().workspace_symbols()->empty());
            update(host, "untitled:buffer", 1, "fn reopened() {}");
            const auto current = host.snapshot().workspace_symbols();
            require_equal(current->size(), 1uz);
            expect_equal(current->front().name, "reopened");
            expect_equal(old->result->back().name, "after");
            expect(!old->document.result->diagnostics().empty());
        };
});

} // namespace
