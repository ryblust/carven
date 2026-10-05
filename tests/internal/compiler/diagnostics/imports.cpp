module carven:test.internal.compiler.diagnostics.imports;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: unused imports are tracked per import declaration"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto used_provider = *sources.append_virtual(
                "used-provider.cv",
                "export fn selected() -> i32 { return 1; }\n"
                "export fn selected_peer() -> i32 { return 3; }\n"
            );
            const auto unused_provider = *sources.append_virtual(
                "unused-provider.cv",
                "export fn spare() -> i32 { return 2; }\n"
            );
            const auto app = *sources.append_virtual(
                "app.cv",
                "import used_provider using { selected, selected_peer, };\n"
                "import unused_provider using spare;\n"
                "fn read() -> i32 { return selected(); }\n"
            );
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = used_provider,
                    .module_path = *CanonicalModulePath::from_value("used_provider"),
                },
                SourceModuleInput {
                    .source_id = unused_provider,
                    .module_path = *CanonicalModulePath::from_value("unused_provider"),
                },
                SourceModuleInput {
                    .source_id = app,
                    .module_path = *CanonicalModulePath::from_value("app"),
                },
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:imports").value(),
                }
            );

            if (!expect(result.has_value())) {
                return;
            }
            const auto* unused =
                find_diagnostic(result->diagnostics, DiagnosticCode::LintUnusedImport);
            if (!expect(unused != nullptr)) {
                return;
            }
            if (!expect(unused->attachment.primary.has_value())) {
                return;
            }
            expect((unused->attachment.primary->span.source_id == app));
            expect_equal(
                sources.slice(unused->attachment.primary->span),
                std::string_view("import unused_provider using spare;")
            );
        };

    "Compiler diagnostics: multiple wildcard providers remain ambiguous at use"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto first =
                *sources.append_virtual("first.cv", "export fn value() -> i32 { return 1; }\n");
            const auto second =
                *sources.append_virtual("second.cv", "export fn value() -> i32 { return 2; }\n");
            const auto app = *sources.append_virtual(
                "app.cv",
                "import first using *;\n"
                "import second using *;\n"
                "const selected = value;\n"
            );
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = first,
                    .module_path = *CanonicalModulePath::from_value("first")
                },
                SourceModuleInput {
                    .source_id = second,
                    .module_path = *CanonicalModulePath::from_value("second")
                },
                SourceModuleInput {
                    .source_id = app,
                    .module_path = *CanonicalModulePath::from_value("app")
                },
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:imports").value(),
                }
            );

            if (!expect(!(result.has_value()))) {
                return;
            }
            if (!expect_equal(result.error().size(), 1u)) {
                return;
            }
            const auto* ambiguous = find_diagnostic(result.error(), DiagnosticCode::NameAmbiguous);
            if (!expect(ambiguous != nullptr)) {
                return;
            }
            if (!expect(ambiguous->attachment.primary.has_value())) {
                return;
            }
            expect_equal(
                sources.slice(ambiguous->attachment.primary->span),
                std::string_view("value")
            );
            expect_equal(ambiguous->attachment.related.size(), 2u);
        };
});

} // namespace
