module carven:test.workspace.imports;

import :semantic.analysis.source;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {

auto apply(std::string text, std::vector<WorkspaceTextEdit> edits) noexcept -> std::string {
    std::ranges::sort(edits, std::greater(), [](const auto& edit) { return edit.range.start(); });
    for (const auto& edit : edits) {
        text.replace(edit.range.start(), edit.range.size(), edit.replacement);
    }
    return text;
}

const TestSuite tests([] static noexcept {
    "Workspace: auto imports use unopened declarations and semantic completion context"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project = std::array {
                WorkspaceProjectModule {"app.cv", *CanonicalModulePath::from_value("pkg.app")},
                WorkspaceProjectModule {"lib.cv", *CanonicalModulePath::from_value("pkg.lib")},
                WorkspaceProjectModule {
                    "foreign.cv",
                    *CanonicalModulePath::from_value("crafts.other.lib")
                }
            };
            require(
                host.update(
                        "lib.cv",
                        1,
                        "fn combine() -> i32 => 1; fn domain_name() -> i32 => 2; private fn hidden() -> i32 => 3; struct Widget { value: i32 }"
                )
                    .has_value()
            );
            require(host.update(
                            "foreign.cv",
                            1,
                            "fn internal() -> i32 => 1; export fn external() -> i32 => 2;"
            )
                        .has_value());
            auto text = std::string("// header\nfn sample() { let broken = ; comb");
            require(host.update("app.cv", 1, text).has_value());
            const auto query = host.snapshot().completion(
                project,
                "app.cv",
                static_cast<std::uint32_t>(text.size())
            );
            const auto named = [&](std::string_view name) {
                return std::ranges::find_if(query.imports, [&](const auto& value) {
                    return value.candidate.name == name;
                });
            };
            require(named("combine") != query.imports.end());
            expect(named("hidden") == query.imports.end());
            expect(named("internal") == query.imports.end());
            expect(named("domain_name") != query.imports.end());
            require(named("external") != query.imports.end());
            expect(named("external")->edit.replacement.contains("other::lib"));
            const auto repaired = apply(text, {named("combine")->edit});
            expect(repaired.starts_with("// header\nimport pkg.lib using combine;\n"));
            text = "fn sample(value: Widgte) {}";
            require(host.update("app.cv", 2, text).has_value());
            const auto type = host.snapshot().completion(
                project,
                "app.cv",
                static_cast<std::uint32_t>(text.find("Widgte") + 3)
            );
            expect(std::ranges::any_of(type.imports, [](const auto& value) static noexcept {
                return value.candidate.name == "Widget";
            }));
            expect(std::ranges::any_of(type.imports, [](const auto& value) static noexcept {
                return value.candidate.name == "Operation";
            }));
            expect(std::ranges::none_of(type.imports, [](const auto& value) static noexcept {
                return value.candidate.kind == SourceCompletionKind::Function;
            }));
            text = "fn sample(combine: i32) { let _ = comb; }";
            require(host.update("app.cv", 3, text).has_value());
            const auto local = host.snapshot().completion(
                project,
                "app.cv",
                static_cast<std::uint32_t>(text.rfind("comb;") + 4)
            );
            expect(std::ranges::none_of(local.imports, [](const auto& value) static noexcept {
                return value.candidate.name == "combine";
            }));
            text = "import .lib using Widget; fn sample(value: Widget) { value.val }";
            require(host.update("app.cv", 4, text).has_value());
            const auto member = host.snapshot().completion(
                project,
                "app.cv",
                static_cast<std::uint32_t>(text.find(".val") + 4)
            );
            expect(member.imports.empty());
        };
    "Workspace: organize imports merges aliases and removes proven unused selections"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project = std::array {
                WorkspaceProjectModule {"app.cv", *CanonicalModulePath::from_value("pkg.app")},
                WorkspaceProjectModule {"lib.cv", *CanonicalModulePath::from_value("pkg.lib")}
            };
            require(host.update(
                            "lib.cv",
                            1,
                            "fn alpha() -> i32 => 1; fn beta() -> i32 => 2; fn gamma() -> i32 => 3;"
            )
                        .has_value());
            auto text = std::string(
                "import .lib using { gamma, beta as b, alpha }\nimport .lib using beta;\nfn sample() -> i32 => alpha() + b() + beta();\n"
            );
            require(host.update("app.cv", 1, text).has_value());
            const auto snapshot = host.snapshot();
            const auto checked = snapshot.semantic(project);
            require(checked.result->program() != nullptr);
            const auto organized = snapshot.organize_imports(project, "app.cv", *checked.result);
            require_equal(organized.size(), 1uz);
            text = apply(text, organized);
            expect(text.starts_with("import .lib using { alpha, beta, beta as b }\n"));
            require(host.update("app.cv", 2, text).has_value());
            const auto after = host.snapshot();
            const auto valid = after.semantic(project);
            require(valid.result->program() != nullptr);
            expect(after.organize_imports(project, "app.cv", *valid.result).empty());
            text =
                "// imports\nimport .lib using { gamma, alpha }\nimport .lib using beta; // attached\nfn sample() { let broken = ; alp";
            require(host.update("app.cv", 3, text).has_value());
            const auto broken = host.snapshot();
            const auto failed = broken.semantic(project);
            text = apply(text, broken.organize_imports(project, "app.cv", *failed.result));
            expect(text.contains("{ alpha, gamma }"));
            expect(text.contains("import .lib using beta; // attached"));
            expect(text.ends_with("let broken = ; alp"));
        };
});

}
