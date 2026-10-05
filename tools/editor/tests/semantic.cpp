module carven:test.editor.semantic;

import :diagnostics.code;
import :editor.analysis;
import :editor.semantic;
import :semantic.evaluation.output;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

auto project_module(std::string document, std::string_view path) noexcept -> editor::ProjectModule {
    auto canonical = CanonicalModulePath::from_value(path);
    ct::require(canonical.has_value());
    return {.document = std::move(document), .module_path = std::move(*canonical)};
}

auto update(
    editor::AnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    ct::require(host.update(std::string(document), version, std::string(text)).has_value());
}

auto expect_version(
    const editor::SemanticQuery& query,
    std::string_view document,
    std::int64_t version
) noexcept -> void {
    const auto found =
        std::ranges::find(query.documents, document, &editor::DocumentVersion::document);
    if (!ct::expect(found != query.documents.end())) {
        return;
    }
    ct::expect_equal(found->version, version);
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "Editor analysis: semantic queries publish cross-module contracts with explicit document identities",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            const auto modules = std::array {
                project_module("file:///workspace/answer.cv", "lib"),
                project_module("untitled:caller", "main"),
            };
            update(host, modules[0].document, 1, "export fn answer() -> i32 { return 42; }");
            update(
                host,
                modules[1].document,
                5,
                "import lib using answer; fn f() -> i32 { return answer(); }"
            );
            const auto snapshot = host.snapshot();
            ct::expect_equal(snapshot.counts().semantic, 0uz);
            const auto query = snapshot.semantic(modules);
            const auto* program = query.result->program();
            if (!ct::expect(program != nullptr)) {
                return;
            }
            ct::expect(query.result->diagnostics().empty());
            ct::expect_equal(query.documents.size(), 2uz);
            expect_version(query, modules[0].document, 1);
            expect_version(query, modules[1].document, 5);
            const auto library = query.result->source(modules[0].document);
            const auto caller = query.result->source(modules[1].document);
            if (!ct::expect(library && caller)) {
                return;
            }
            ct::expect_equal(library->origin, modules[0].document);
            ct::expect_equal(caller->origin, modules[1].document);
            ct::expect(library->source_id != caller->source_id);
            ct::expect(!query.result->source("lib"));
            ct::expect_equal(program->declarations().functions().size(), 2uz);
            auto names = std::vector<std::string>();
            for (const auto entry : program->declarations().functions()) {
                names.emplace_back(program->provenance().spelling(entry.value.name));
                const auto& callable = program->declarations().callable(entry.value.callable);
                const auto& signature =
                    program->callable_signatures().signature(callable.signature);
                ct::expect(signature.result == program->types().builtin_type(BuiltinType::I32));
                ct::expect(signature.parameters.empty());
                ct::expect(callable_body_id(callable).has_value());
            }
            std::ranges::sort(names);
            ct::expect_equal(names, std::vector<std::string>({"answer", "f"}));
            ct::expect(snapshot.semantic(modules).result == query.result);
            ct::expect_equal(snapshot.counts().semantic, 1uz);
        }
    );

    ct::test(
        "Editor analysis: semantic content caches ignore project order and client versions",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "lib.cv", 1, "export fn answer() -> i32 { return 42; }");
            update(
                host,
                "caller.cv",
                1,
                "import lib using answer; fn f() -> i32 { return answer(); }"
            );
            update(host, "unrelated.cv", 1, "fn unrelated() {}");
            const auto modules =
                std::array {project_module("lib.cv", "lib"), project_module("caller.cv", "main")};
            const auto reordered = std::array {modules[1], modules[0]};
            const auto before = host.snapshot();
            const auto original = before.semantic(modules);
            ct::require(original.result->program() != nullptr);
            ct::expect(before.semantic(reordered).result == original.result);
            update(host, "lib.cv", 2, "export fn answer() -> i32 { return 42; }");
            update(host, "unrelated.cv", 2, "fn changed() {}");
            const auto after = host.snapshot();
            const auto current = after.semantic(reordered);
            ct::expect(current.result == original.result);
            expect_version(original, "lib.cv", 1);
            expect_version(current, "lib.cv", 2);
            expect_version(current, "caller.cv", 1);
            ct::expect_equal(current.documents.size(), 2uz);
            ct::expect_equal(after.counts().semantic, 1uz);
            update(host, "lib.cv", 3, "export fn answer() -> i32 { return 43; }");
            const auto edited_snapshot = host.snapshot();
            const auto edited = edited_snapshot.semantic(modules);
            ct::expect(edited.result != original.result);
            ct::expect(edited.result->program() != nullptr);
            expect_version(edited, "lib.cv", 3);
            const auto old_source = original.result->source("lib.cv");
            const auto new_source = edited.result->source("lib.cv");
            if (!ct::expect(old_source && new_source)) {
                return;
            }
            ct::expect_equal(old_source->text, "export fn answer() -> i32 { return 42; }");
            ct::expect_equal(new_source->text, "export fn answer() -> i32 { return 43; }");
            ct::expect_equal(edited_snapshot.counts().semantic, 2uz);
        }
    );

    ct::test(
        "Editor analysis: explicit module mapping changes invalidate semantic results",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "export fn answer() -> i32 { return 42; }");
            update(host, "b.cv", 1, "import lib using answer; fn f() -> i32 { return answer(); }");
            const auto modules =
                std::array {project_module("a.cv", "lib"), project_module("b.cv", "main")};
            const auto renamed =
                std::array {project_module("a.cv", "renamed"), project_module("b.cv", "main")};
            const auto snapshot = host.snapshot();
            const auto valid = snapshot.semantic(modules);
            ct::require(valid.result->program() != nullptr);
            const auto invalid = snapshot.semantic(renamed);
            ct::expect(invalid.result != valid.result);
            ct::expect(invalid.result->program() == nullptr);
            ct::expect_diagnostic(invalid.result->diagnostics(), DiagnosticCode::ImportResolution);
            ct::expect(snapshot.semantic(renamed).result == invalid.result);
            ct::expect(snapshot.semantic(modules).result == valid.result);
            ct::expect_equal(snapshot.counts().semantic, 2uz);
        }
    );

    ct::test(
        "Editor analysis: invalid project selections cache structured input diagnostics",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn a() {}");
            update(host, "b.cv", 1, "fn b() {}");
            struct Scenario final {
                std::string_view name;
                std::vector<editor::ProjectModule> modules;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "missing document",
                    .modules = {project_module("missing.cv", "missing")}
                },
                Scenario {
                    .name = "duplicate module path",
                    .modules = {project_module("a.cv", "same"), project_module("b.cv", "same")}
                },
                Scenario {
                    .name = "duplicate document",
                    .modules = {project_module("a.cv", "first"), project_module("a.cv", "second")}
                },
                Scenario {.name = "empty project", .modules = {}},
            };
            const auto snapshot = host.snapshot();
            ct::each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                const auto query = snapshot.semantic(scenario.modules);
                ct::expect(query.result->program() == nullptr);
                ct::expect_diagnostic(
                    query.result->diagnostics(),
                    DiagnosticCode::CompilationInput
                );
                ct::expect(snapshot.semantic(scenario.modules).result == query.result);
                ct::expect(query.result->output().empty());
            });
            ct::expect_equal(snapshot.counts().semantic, scenarios.size());
            const auto missing = std::array {project_module("missing.cv", "missing")};
            const auto absent = snapshot.semantic(missing);
            update(host, "missing.cv", 1, "fn available() {}");
            const auto repaired = host.snapshot().semantic(missing);
            ct::expect(repaired.result != absent.result);
            ct::expect(repaired.result->program() != nullptr);
            ct::expect(repaired.result->diagnostics().empty());
        }
    );

    ct::test(
        "Editor analysis: failed semantic queries retain diagnostics and source owners after repair",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view text;
                DiagnosticCode code;
            };
            const auto scenarios = std::array {
                Scenario {.name = "syntax", .text = "fn broken(", .code = DiagnosticCode::Syntax},
                Scenario {
                    .name = "semantic",
                    .text = "fn broken() -> i32 { return missing; }",
                    .code = DiagnosticCode::NameUnresolved
                },
            };
            ct::each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                const auto retained = [&]() noexcept {
                    auto host = editor::AnalysisHost();
                    const auto modules = std::array {project_module("untitled:broken", "main")};
                    update(host, "untitled:broken", 1, scenario.text);
                    const auto before = host.snapshot();
                    const auto failed = before.semantic(modules);
                    ct::expect(failed.result->program() == nullptr);
                    ct::expect_diagnostic(failed.result->diagnostics(), scenario.code);
                    ct::expect(before.semantic(modules).result == failed.result);
                    update(host, "untitled:broken", 2, "fn repaired() -> i32 { return 42; }");
                    const auto after = host.snapshot();
                    const auto repaired = after.semantic(modules);
                    ct::expect(repaired.result != failed.result);
                    ct::expect(repaired.result->program() != nullptr);
                    ct::expect(repaired.result->diagnostics().empty());
                    ct::expect(before.semantic(modules).result == failed.result);
                    ct::expect_equal(after.counts().semantic, 2uz);
                    return failed;
                }();
                const auto source = retained.result->source("untitled:broken");
                if (!ct::expect(source.has_value())) {
                    return;
                }
                ct::expect_equal(source->text, scenario.text);
                expect_version(retained, "untitled:broken", 1);
                const auto* diagnostic =
                    ct::find_diagnostic(retained.result->diagnostics(), scenario.code);
                if (!ct::expect(diagnostic && diagnostic->attachment.primary)) {
                    return;
                }
                ct::expect_equal(
                    retained.result->sources()
                        .view(diagnostic->attachment.primary->span.source_id)
                        .origin,
                    "untitled:broken"
                );
                ct::expect(
                    try_slice(source->text, diagnostic->attachment.primary->span.span).has_value()
                );
            });
        }
    );

    ct::test(
        "Editor analysis: semantic static output is captured once per content computation",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            const auto modules = std::array {project_module("output.cv", "main")};
            update(host, "output.cv", 1, "const { println(\"captured\"); } fn f() {}");
            const auto before = host.snapshot();
            const auto original = before.semantic(modules);
            if (!ct::expect(original.result->program() != nullptr)) {
                return;
            }
            auto output = std::string();
            for (const auto& chunk : original.result->output()) {
                ct::expect_equal(chunk.stream, ExecutionOutputStream::Standard);
                output += chunk.bytes;
            }
            ct::expect_equal(output, "captured\n");
            ct::expect(before.semantic(modules).result == original.result);
            update(host, "output.cv", 2, "const { println(\"captured\"); } fn f() {}");
            const auto after = host.snapshot();
            const auto unchanged = after.semantic(modules);
            ct::expect(unchanged.result == original.result);
            expect_version(unchanged, "output.cv", 2);
            ct::expect_equal(after.counts().semantic, 1uz);
            update(host, "output.cv", 3, "const { println(\"changed\"); } fn f() {}");
            const auto edited_snapshot = host.snapshot();
            const auto edited = edited_snapshot.semantic(modules);
            ct::expect(edited.result != original.result);
            auto changed_output = std::string();
            for (const auto& chunk : edited.result->output()) {
                changed_output += chunk.bytes;
            }
            ct::expect_equal(changed_output, "changed\n");
            ct::expect_equal(edited_snapshot.counts().semantic, 2uz);
        }
    );
});

} // namespace
