module carven:test.workspace.semantic;

import :diagnostics.code;
import :semantic.evaluation.output;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :source.text;
import :support.timing;
import :test.harness.diagnostics;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.semantic;
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
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    require(host.update(std::string(document), version, std::string(text)).has_value());
}

auto expect_version(
    const WorkspaceSemanticQuery& query,
    std::string_view document,
    std::int64_t version
) noexcept -> void {
    const auto found =
        std::ranges::find(query.documents, document, &WorkspaceDocumentVersion::document);
    if (!expect(found != query.documents.end())) {
        return;
    }
    expect_equal(found->version, version);
}

const TestSuite tests([] static noexcept {
    "Workspace analysis: timing recipients observe computation without entering cache identity"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto modules = std::array {project_module("timed", "main")};
            const auto text =
                std::string("const { println(\"timed\"); } fn f() -> i32 { return 1; }");
            update(host, "timed", 1, text);
            const auto original = [&]() noexcept {
                auto stages = std::vector<TimingStage>();
                const auto capture = [&](TimingStage stage,
                                         std::chrono::steady_clock::duration elapsed) noexcept {
                    expect(elapsed >= std::chrono::steady_clock::duration::zero());
                    stages.push_back(stage);
                };
                const auto query = host.snapshot().semantic(modules, capture);
                if (!expect(query.result->program() != nullptr)) {
                    return query.result;
                }
                for (const auto stage : std::array {
                         TimingStage::SourceLoading,
                         TimingStage::Lexing,
                         TimingStage::Parsing,
                         TimingStage::SemanticAnalysis,
                         TimingStage::SemanticCatalog,
                         TimingStage::SemanticDeclarations,
                         TimingStage::SemanticBodies,
                         TimingStage::SemanticSolving,
                         TimingStage::SemanticValidation,
                         TimingStage::SourceObservations,
                         TimingStage::SourceIndex
                     }) {
                    expect(std::ranges::find(stages, stage) != stages.end());
                }
                expect(query.result->diagnostics().empty());
                return query.result;
            }();
            auto calls = 0uz;
            const auto capture = [&](TimingStage, std::chrono::steady_clock::duration) noexcept {
                ++calls;
            };
            expect(host.snapshot().semantic(modules, capture).result == original);
            expect_equal(calls, 0uz);
            update(host, "timed", 2, text);
            expect(host.snapshot().semantic(modules, capture).result == original);
            expect_equal(calls, 0uz);
            expect_equal(host.snapshot().counts().semantic, 1uz);
            auto output = std::string();
            for (const auto& chunk : original->output()) {
                output += chunk.bytes;
            }
            expect_equal(output, "timed\n");
            // The first recipient is dead; new computations must not retain it.
            update(host, "timed", 3, "fn f() -> i32 { return 2; }");
            expect(host.snapshot().semantic(modules).result->program() != nullptr);
            auto stages = std::vector<TimingStage>();
            const auto rejected = [&](TimingStage stage,
                                      std::chrono::steady_clock::duration) noexcept {
                stages.push_back(stage);
            };
            update(host, "timed", 4, "fn f() { let broken = ; }");
            const auto broken = host.snapshot().semantic(modules, rejected);
            expect(broken.result->program() == nullptr);
            expect(!broken.result->diagnostics().empty());
            expect(std::ranges::find(stages, TimingStage::Parsing) != stages.end());
            expect(std::ranges::find(stages, TimingStage::SemanticAnalysis) == stages.end());
        };

    "Workspace analysis: semantic queries publish cross-module contracts with explicit document identities"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
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
            expect_equal(snapshot.counts().semantic, 0uz);
            const auto query = snapshot.semantic(modules);
            const auto* program = query.result->program();
            if (!expect(program != nullptr)) {
                return;
            }
            expect(query.result->diagnostics().empty());
            expect_equal(query.documents.size(), 2uz);
            expect_version(query, modules[0].document, 1);
            expect_version(query, modules[1].document, 5);
            const auto library = query.result->source(modules[0].document);
            const auto caller = query.result->source(modules[1].document);
            if (!expect(library && caller)) {
                return;
            }
            expect_equal(library->origin, modules[0].document);
            expect_equal(caller->origin, modules[1].document);
            expect(library->source_id != caller->source_id);
            expect(!query.result->source("lib"));
            expect_equal(program->declarations().functions().size(), 2uz);
            auto names = std::vector<std::string>();
            for (const auto entry : program->declarations().functions()) {
                names.emplace_back(program->provenance().spelling(entry.value.name));
                const auto& callable = program->declarations().callable(entry.value.callable);
                const auto& signature =
                    program->callable_signatures().signature(callable.signature);
                expect(signature.result == program->types().builtin_type(BuiltinType::I32));
                expect(signature.parameters.empty());
                expect(callable_body_id(callable).has_value());
            }
            std::ranges::sort(names);
            expect_equal(names, std::vector<std::string>({"answer", "f"}));
            expect(snapshot.semantic(modules).result == query.result);
            expect_equal(snapshot.counts().semantic, 1uz);
        };

    "Workspace analysis: semantic content caches ignore project order and client versions"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
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
            require(original.result->program() != nullptr);
            expect(before.semantic(reordered).result == original.result);
            update(host, "lib.cv", 2, "export fn answer() -> i32 { return 42; }");
            update(host, "unrelated.cv", 2, "fn changed() {}");
            const auto after = host.snapshot();
            const auto current = after.semantic(reordered);
            expect(current.result == original.result);
            expect_version(original, "lib.cv", 1);
            expect_version(current, "lib.cv", 2);
            expect_version(current, "caller.cv", 1);
            expect_equal(current.documents.size(), 2uz);
            expect_equal(after.counts().semantic, 1uz);
            update(host, "lib.cv", 3, "export fn answer() -> i32 { return 43; }");
            const auto edited_snapshot = host.snapshot();
            const auto edited = edited_snapshot.semantic(modules);
            expect(edited.result != original.result);
            expect(edited.result->program() != nullptr);
            expect_version(edited, "lib.cv", 3);
            const auto old_source = original.result->source("lib.cv");
            const auto new_source = edited.result->source("lib.cv");
            if (!expect(old_source && new_source)) {
                return;
            }
            expect_equal(old_source->text, "export fn answer() -> i32 { return 42; }");
            expect_equal(new_source->text, "export fn answer() -> i32 { return 43; }");
            expect_equal(edited_snapshot.counts().semantic, 2uz);
        };

    "Workspace analysis: explicit module mapping changes invalidate semantic results"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 1, "export fn answer() -> i32 { return 42; }");
            update(host, "b.cv", 1, "import lib using answer; fn f() -> i32 { return answer(); }");
            const auto modules =
                std::array {project_module("a.cv", "lib"), project_module("b.cv", "main")};
            const auto renamed =
                std::array {project_module("a.cv", "renamed"), project_module("b.cv", "main")};
            const auto snapshot = host.snapshot();
            const auto valid = snapshot.semantic(modules);
            require(valid.result->program() != nullptr);
            const auto invalid = snapshot.semantic(renamed);
            expect(invalid.result != valid.result);
            expect(invalid.result->program() == nullptr);
            expect_diagnostic(invalid.result->diagnostics(), DiagnosticCode::ImportResolution);
            expect(snapshot.semantic(renamed).result == invalid.result);
            const auto restored = snapshot.semantic(modules);
            expect(restored.result->program() != nullptr);
            expect(restored.result->diagnostics().empty());
        };

    "Workspace analysis: invalid project selections cache structured input diagnostics"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 1, "fn a() {}");
            update(host, "b.cv", 1, "fn b() {}");
            struct Scenario final {
                std::string_view name;
                std::vector<WorkspaceProjectModule> modules;
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
            each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                const auto query = snapshot.semantic(scenario.modules);
                expect(query.result->program() == nullptr);
                expect_diagnostic(query.result->diagnostics(), DiagnosticCode::CompilationInput);
                expect(snapshot.semantic(scenario.modules).result == query.result);
                expect(query.result->output().empty());
            });
            expect_equal(snapshot.counts().semantic, scenarios.size());
            const auto missing = std::array {project_module("missing.cv", "missing")};
            const auto absent = snapshot.semantic(missing);
            update(host, "missing.cv", 1, "fn available() {}");
            const auto repaired = host.snapshot().semantic(missing);
            expect(repaired.result != absent.result);
            expect(repaired.result->program() != nullptr);
            expect(repaired.result->diagnostics().empty());
        };

    "Workspace analysis: failed semantic queries retain diagnostics and source owners after repair"_test =
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
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                const auto retained = [&]() noexcept {
                    auto host = WorkspaceAnalysisHost();
                    const auto modules = std::array {project_module("untitled:broken", "main")};
                    update(host, "untitled:broken", 1, scenario.text);
                    const auto before = host.snapshot();
                    const auto failed = before.semantic(modules);
                    expect(failed.result->program() == nullptr);
                    expect_diagnostic(failed.result->diagnostics(), scenario.code);
                    expect(before.semantic(modules).result == failed.result);
                    update(host, "untitled:broken", 2, "fn repaired() -> i32 { return 42; }");
                    const auto after = host.snapshot();
                    const auto repaired = after.semantic(modules);
                    expect(repaired.result != failed.result);
                    expect(repaired.result->program() != nullptr);
                    expect(repaired.result->diagnostics().empty());
                    expect(before.semantic(modules).result == failed.result);
                    expect_equal(after.counts().semantic, 2uz);
                    return failed;
                }();
                const auto source = retained.result->source("untitled:broken");
                if (!expect(source.has_value())) {
                    return;
                }
                expect_equal(source->text, scenario.text);
                expect_version(retained, "untitled:broken", 1);
                const auto* diagnostic =
                    find_diagnostic(retained.result->diagnostics(), scenario.code);
                if (!expect(diagnostic && diagnostic->attachment.primary)) {
                    return;
                }
                expect_equal(
                    retained.result->sources()
                        .view(diagnostic->attachment.primary->span.source_id)
                        .origin,
                    "untitled:broken"
                );
                expect(
                    try_slice(source->text, diagnostic->attachment.primary->span.span).has_value()
                );
            });
        };

    "Workspace analysis: semantic static output is captured once per content computation"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto modules = std::array {project_module("output.cv", "main")};
            update(host, "output.cv", 1, "const { println(\"captured\"); } fn f() {}");
            const auto before = host.snapshot();
            const auto original = before.semantic(modules);
            if (!expect(original.result->program() != nullptr)) {
                return;
            }
            auto output = std::string();
            for (const auto& chunk : original.result->output()) {
                expect_equal(chunk.stream, ExecutionOutputStream::Standard);
                output += chunk.bytes;
            }
            expect_equal(output, "captured\n");
            expect(before.semantic(modules).result == original.result);
            update(host, "output.cv", 2, "const { println(\"captured\"); } fn f() {}");
            const auto after = host.snapshot();
            const auto unchanged = after.semantic(modules);
            expect(unchanged.result == original.result);
            expect_version(unchanged, "output.cv", 2);
            expect_equal(after.counts().semantic, 1uz);
            update(host, "output.cv", 3, "const { println(\"changed\"); } fn f() {}");
            const auto edited_snapshot = host.snapshot();
            const auto edited = edited_snapshot.semantic(modules);
            expect(edited.result != original.result);
            auto changed_output = std::string();
            for (const auto& chunk : edited.result->output()) {
                changed_output += chunk.bytes;
            }
            expect_equal(changed_output, "changed\n");
            expect_equal(edited_snapshot.counts().semantic, 2uz);
        };
});

} // namespace
