module carven:test.editor.invalidation;

import :editor.analysis;
import :editor.semantic;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :source.provenance;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto project_module(std::string document, std::string_view path) noexcept -> EditorProjectModule {
    auto canonical = CanonicalModulePath::from_value(path);
    require(canonical.has_value());
    return {.document = std::move(document), .module_path = std::move(*canonical)};
}

auto update(
    EditorAnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    require(host.update(std::string(document), version, std::string(text)).has_value());
}

auto offset(std::string_view text, std::string_view needle, bool last = false) noexcept
    -> std::uint32_t {
    const auto position = last ? text.rfind(needle) : text.find(needle);
    require(position != std::string_view::npos);
    return static_cast<std::uint32_t>(position);
}

auto expect_type(const EditorHoverQuery& query, BuiltinType type) noexcept -> void {
    const auto* program = query.analysis.result->program();
    if (!expect(program != nullptr && query.result.has_value())) {
        return;
    }
    const auto* published = std::get_if<TypeID>(&query.result->type);
    expect(published != nullptr && *published == program->types().builtin_type(type));
}

auto expect_inferred_result(const EditorSemanticQuery& query, BuiltinType type) noexcept -> void {
    const auto* program = query.result->program();
    if (!expect(program != nullptr)) {
        return;
    }
    const auto provenance = program->provenance();
    for (const auto function : program->declarations().functions()) {
        if (provenance.spelling(function.value.name) != "inferred") {
            continue;
        }
        const auto& callable = program->declarations().callable(function.value.callable);
        const auto& signature = program->callable_signatures().signature(callable.signature);
        expect(signature.result == program->types().builtin_type(type));
        return;
    }
    expect(false).note("The analyzed caller must retain its inferred declaration");
}

auto expect_definition(
    const EditorDefinitionQuery& query,
    std::string_view document,
    std::int64_t version
) noexcept -> void {
    if (!expect(query.result.has_value())) {
        return;
    }
    expect_equal(query.result->document, document);
    expect_equal(query.result->version, version);
    const auto source = query.analysis.result->source(document);
    if (!expect(source.has_value())) {
        return;
    }
    expect_equal(slice(source->text, query.result->range), "answer");
}

constexpr auto library = std::string_view("export fn answer() -> i32 { return 42; }");
constexpr auto caller = std::string_view(
    "import lib using answer; fn inferred() => answer(); fn probe() { let value = answer(); }"
);

const TestSuite tests([] static noexcept {
    "Editor analysis: dependency body and signature edits refresh consumer inference"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view text;
                BuiltinType type;
            };
            const auto scenarios = std::array {
                Scenario {.name = "initial", .text = library, .type = BuiltinType::I32},
                Scenario {
                    .name = "body edit",
                    .text = "export fn answer() -> i32 { return 43; }",
                    .type = BuiltinType::I32
                },
                Scenario {
                    .name = "signature edit",
                    .text = "export fn answer() -> i64 { return 43; }",
                    .type = BuiltinType::I64
                },
            };
            auto host = EditorAnalysisHost();
            update(host, "caller.cv", 7, caller);
            const auto modules = std::array {
                project_module("lib.cv", "lib"),
                project_module("caller.cv", "main"),
            };
            auto version = 0ll;
            auto previous = std::shared_ptr<const EditorSemanticAnalysis>();
            each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                update(host, "lib.cv", ++version, scenario.text);
                const auto snapshot = host.snapshot();
                const auto current = snapshot.hover(modules, "caller.cv", offset(caller, "value"));
                expect_type(current, scenario.type);
                expect_inferred_result(current.analysis, scenario.type);
                expect_definition(
                    snapshot.definition(modules, "caller.cv", offset(caller, "answer()", true)),
                    "lib.cv",
                    version
                );
                expect(current.analysis.result != previous);
                expect(snapshot.semantic(modules).result == current.analysis.result);
                expect_equal(snapshot.counts().semantic, static_cast<std::size_t>(version));
                previous = current.analysis.result;
            });
        };

    "Editor analysis: broken dependency transitions never reuse stale navigation"_test =
        [] static noexcept {
            enum class Change {
                RenameExport,
                WithdrawExport,
                RenameImport,
                RemoveDocument,
                OmitModule
            };
            struct Scenario final {
                std::string_view name;
                Change change;
            };
            const auto scenarios = std::array {
                Scenario {.name = "rename export", .change = Change::RenameExport},
                Scenario {.name = "withdraw export", .change = Change::WithdrawExport},
                Scenario {.name = "rename import", .change = Change::RenameImport},
                Scenario {.name = "remove dependency document", .change = Change::RemoveDocument},
                Scenario {.name = "omit dependency module", .change = Change::OmitModule},
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto host = EditorAnalysisHost();
                update(host, "lib.cv", 1, library);
                update(host, "caller.cv", 1, caller);
                const auto modules = std::array {
                    project_module("lib.cv", "lib"),
                    project_module("caller.cv", "main"),
                };
                const auto original = host.snapshot();
                const auto call = offset(caller, "answer()", true);
                const auto before = original.definition(modules, "caller.cv", call);
                expect_definition(before, "lib.cv", 1);
                auto project = std::vector<EditorProjectModule>(modules.begin(), modules.end());
                switch (scenario.change) {
                    case Change::RenameExport:
                        update(host, "lib.cv", 2, "export fn renamed() -> i32 { return 42; }");
                        break;
                    case Change::WithdrawExport:
                        update(host, "lib.cv", 2, "private fn answer() -> i32 { return 42; }");
                        break;
                    case Change::RenameImport:
                        update(
                            host,
                            "caller.cv",
                            2,
                            "import absent using answer; fn inferred() => answer(); fn probe() { let value = answer(); }"
                        );
                        break;
                    case Change::RemoveDocument: expect(host.remove("lib.cv")); break;
                    case Change::OmitModule:     project.erase(project.begin()); break;
                }
                const auto broken = host.snapshot();
                const auto failed = broken.definition(project, "caller.cv", call);
                expect(failed.analysis.result != before.analysis.result);
                expect(failed.analysis.result->program() == nullptr);
                expect(!failed.analysis.result->diagnostics().empty());
                expect(!failed.result);
                expect(!broken.hover(project, "caller.cv", call).result);
                expect(broken.semantic(project).result == failed.analysis.result);
                expect_definition(before, "lib.cv", 1);
                expect_equal(broken.counts().semantic, 2uz);
                update(host, "lib.cv", 3, library);
                update(host, "caller.cv", 3, caller);
                const auto repaired = host.snapshot();
                const auto after = repaired.definition(modules, "caller.cv", call);
                expect_definition(after, "lib.cv", 3);
                expect(after.analysis.result != failed.analysis.result);
                expect(failed.analysis.result->program() == nullptr);
                expect(!failed.analysis.result->diagnostics().empty());
                expect_equal(repaired.counts().semantic, 3uz);
                expect(after.analysis.result != before.analysis.result);
            });
        };

    "Editor analysis: navigation caches follow selected content rather than unrelated edits"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "lib.cv", 1, library);
            update(host, "caller.cv", 1, caller);
            update(host, "unselected.cv", 1, "fn unused() {}");
            const auto modules = std::array {
                project_module("lib.cv", "lib"),
                project_module("caller.cv", "main"),
            };
            const auto call = offset(caller, "answer()", true);
            const auto first = host.snapshot().definition(modules, "caller.cv", call);
            expect_definition(first, "lib.cv", 1);
            update(host, "unselected.cv", 2, "fn broken() -> i32 { return unknown; }");
            update(host, "lib.cv", 2, library);
            const auto reused = host.snapshot().definition(modules, "caller.cv", call);
            expect_definition(reused, "lib.cv", 2);
            expect(reused.analysis.result == first.analysis.result);
            expect_equal(host.snapshot().counts().semantic, 1uz);
            const auto selected_edit = std::string("// shifted\n") + std::string(caller);
            update(host, "caller.cv", 2, selected_edit);
            const auto refreshed =
                host.snapshot().hover(modules, "caller.cv", offset(selected_edit, "value"));
            expect_type(refreshed, BuiltinType::I32);
            expect_inferred_result(refreshed.analysis, BuiltinType::I32);
            expect(refreshed.analysis.result != first.analysis.result);
            expect_equal(host.snapshot().counts().semantic, 2uz);
            expect_definition(first, "lib.cv", 1);
        };

    "Editor analysis: module selections retain only the current project or explicit owners"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "a.cv", 1, "fn a() {}");
            update(host, "b.cv", 1, "fn b() {}");
            const auto first = std::array {project_module("a.cv", "a")};
            const auto second = std::array {project_module("b.cv", "b")};
            const auto both = std::array {first[0], second[0]};
            struct Selection final {
                std::string_view name;
                std::span<const EditorProjectModule> modules;
            };
            const auto selections = std::array {
                Selection {.name = "first", .modules = first},
                Selection {.name = "second", .modules = second},
                Selection {.name = "both", .modules = both}
            };
            auto previous = std::weak_ptr<const EditorSemanticAnalysis>();
            each(selections, &Selection::name, [&](const Selection& selected) noexcept {
                const auto query = host.snapshot().semantic(selected.modules);
                expect(query.result->program() != nullptr);
                expect(previous.expired());
                expect(host.snapshot().semantic(selected.modules).result == query.result);
                previous = query.result;
            });
            expect(!previous.expired());
            expect_equal(host.snapshot().counts().semantic, selections.size());
            auto snapshot = std::optional(host.snapshot());
            auto held = std::optional(snapshot->semantic(both));
            update(host, "unselected.cv", 1, "fn unselected() {}");
            const auto current = host.snapshot().semantic(first);
            expect(current.result != held->result);
            expect(!previous.expired());
            snapshot.reset();
            expect(!previous.expired());
            expect(held->result->source("b.cv").has_value());
            held.reset();
            expect(previous.expired());
        };

    "Editor analysis: replacing selected content releases unobserved semantic generations"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "lib.cv", 1, library);
            update(host, "caller.cv", 1, caller);
            const auto modules = std::array {
                project_module("lib.cv", "lib"),
                project_module("caller.cv", "main"),
            };
            const auto use = offset(caller, "value");
            auto previous = std::weak_ptr<const EditorSemanticAnalysis>();
            for (auto version = 1ll; version <= 6ll; ++version) {
                scenario(std::format("generation {}", version), [&]() noexcept {
                    if (version != 1ll) {
                        const auto text = std::format(
                            "export fn answer() -> i32 {{ return {}; }}",
                            41ll + version
                        );
                        update(host, "lib.cv", version, text);
                    }
                    const auto current = host.snapshot().hover(modules, "caller.cv", use);
                    expect_type(current, BuiltinType::I32);
                    expect(previous.expired());
                    previous = current.analysis.result;
                });
            }
            expect(!previous.expired());
            expect_equal(host.snapshot().counts().semantic, 6uz);
            expect(host.remove("lib.cv"));
            const auto failed = host.snapshot().semantic(modules);
            expect(failed.result->program() == nullptr);
            expect(previous.expired());
        };

    "Editor analysis: invalidated semantic owners release before another project query"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                bool remove;
                bool retain;
            };
            const auto scenarios = std::array {
                Scenario {.name = "edit without old owners", .remove = false, .retain = false},
                Scenario {.name = "remove without old owners", .remove = true, .retain = false},
                Scenario {.name = "edit with old owners", .remove = false, .retain = true},
                Scenario {.name = "remove with old owners", .remove = true, .retain = true},
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto host = EditorAnalysisHost();
                update(host, "lib.cv", 1, library);
                update(host, "caller.cv", 1, caller);
                const auto modules = std::array {
                    project_module("lib.cv", "lib"),
                    project_module("caller.cv", "main"),
                };
                auto snapshot = std::optional(host.snapshot());
                auto held = std::optional(
                    snapshot->definition(modules, "caller.cv", offset(caller, "answer()", true))
                );
                expect_definition(*held, "lib.cv", 1);
                const auto observed =
                    std::weak_ptr<const EditorSemanticAnalysis>(held->analysis.result);
                if (!scenario.retain) {
                    snapshot.reset();
                    held.reset();
                }
                expect(!observed.expired());
                if (scenario.remove) {
                    expect(host.remove("lib.cv"));
                } else {
                    update(host, "lib.cv", 2, "export fn answer() -> i32 { return 43; }");
                }
                expect_equal(host.snapshot().counts().semantic, 1uz);
                if (!scenario.retain) {
                    expect(observed.expired());
                    return;
                }
                expect(!observed.expired());
                expect_definition(*held, "lib.cv", 1);
                snapshot.reset();
                expect(!observed.expired());
                held.reset();
                expect(observed.expired());
            });
        };

    "Editor analysis: old snapshots and query owners retain only their requested generations"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "lib.cv", 1, library);
            update(host, "caller.cv", 1, caller);
            const auto modules = std::array {
                project_module("lib.cv", "lib"),
                project_module("caller.cv", "main"),
            };
            const auto call = offset(caller, "answer()", true);
            auto snapshot = std::optional(host.snapshot());
            auto retained = std::optional(snapshot->definition(modules, "caller.cv", call));
            const auto observed =
                std::weak_ptr<const EditorSemanticAnalysis>(retained->analysis.result);
            update(host, "lib.cv", 2, "export fn answer() -> i64 { return 42; }");
            const auto current =
                host.snapshot().hover(modules, "caller.cv", offset(caller, "value"));
            expect_type(current, BuiltinType::I64);
            expect_definition(*retained, "lib.cv", 1);
            expect_definition(snapshot->definition(modules, "caller.cv", call), "lib.cv", 1);
            expect(!observed.expired());
            snapshot.reset();
            expect(!observed.expired());
            expect_definition(*retained, "lib.cv", 1);
            retained.reset();
            expect(observed.expired());
            expect_type(current, BuiltinType::I64);
        };
});

} // namespace
