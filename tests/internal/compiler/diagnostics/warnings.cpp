module carven:test.internal.compiler.diagnostics.warnings;

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
import :test.internal.source.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: implicit standard dependencies do not consume unused imports"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto inputs = std::vector<SourceModuleInput>();
            constexpr auto modules = std::array {
                std::pair<std::string_view, std::string_view> {
                    "main",
                    "import std::time using timeout;\n\nimport std::time using Duration; "
                    "import std::time using TimeRangeError; "
                    "import std::time using Timeout; "
                    "async fn leaf() -> i32 => 7; "
                    "const delay = 1ms; fn run() { let pending = timeout(leaf(), delay); }"
                },
            };
            for (const auto& [path, source] : modules) {
                const auto id =
                    sources.append_virtual(std::string(path) + ".cv", std::string(source));
                require(id.has_value());
                inputs.push_back(
                    {.source_id = *id, .module_path = *CanonicalModulePath::from_value(path)}
                );
            }
            constexpr auto standard_modules = std::to_array<std::string_view>({"time"});
            append_standard_sources(sources, inputs, standard_modules);
            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain =
                        LinkageDomain::explicit_value("test:timeout-type-imports").value(),
                    .libraries = {},
                }
            );
            if (!expect(result.has_value())) {
                return;
            }
            auto unused = std::set<std::string_view>();
            for (const auto& diagnostic : result->diagnostics) {
                if (diagnostic.finding.code != DiagnosticCode::LintUnusedImport) {
                    continue;
                }
                require(diagnostic.attachment.primary.has_value());
                unused.insert(sources.slice(diagnostic.attachment.primary->span));
            }
            const auto expected = std::set<std::string_view> {
                "import std::time using Duration;",
                "import std::time using TimeRangeError;",
                "import std::time using Timeout;",
            };
            expect(unused == expected);
        };

    "Compiler diagnostics: successful compilation retains warning location"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = std::string(
                "fn value(parameter: i32) {\n"
                "    let unused = 1;\n"
                "    return;\n"
                "    let unreachable = 1;\n"
                "}\n"
            );
            const auto source_id = *sources.append_virtual("warning.cv", source);
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("warning"),
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:semantics").value(),
                    .libraries = {},
                }
            );

            if (!expect(result.has_value())) {
                return;
            }
            const auto* unreachable =
                find_diagnostic(result->diagnostics, DiagnosticCode::FlowUnreachable);
            if (!expect(unreachable != nullptr)) {
                return;
            }
            if (!expect(unreachable->attachment.primary.has_value())) {
                return;
            }
            expect_equal(sources.location(unreachable->attachment.primary->span).line, 4u);
            expect_equal(
                sources.slice(unreachable->attachment.primary->span),
                std::string_view("let unreachable = 1;")
            );

            const auto* unused =
                find_diagnostic(result->diagnostics, DiagnosticCode::LintUnusedLocal);
            if (!expect(unused != nullptr)) {
                return;
            }
            if (!expect(unused->attachment.primary.has_value())) {
                return;
            }
            expect_equal(
                sources.slice(unused->attachment.primary->span),
                std::string_view("unused")
            );

            const auto* parameter =
                find_diagnostic(result->diagnostics, DiagnosticCode::LintUnusedParameter);
            if (!expect(parameter != nullptr)) {
                return;
            }
            if (!expect(parameter->attachment.primary.has_value())) {
                return;
            }
            expect_equal(
                sources.slice(parameter->attachment.primary->span),
                std::string_view("parameter")
            );
        };

    "Compiler diagnostics: global references do not consume explicit C++ imports"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual(
                "global.cv",
                "import <native> using value; fn f() { ::value(); }"
            );
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("global"),
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1uz)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:global-unused").value(),
                    .libraries = {},
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
            expect_equal(
                sources.slice(unused->attachment.primary->span),
                std::string_view("value")
            );
        };

    "Compiler diagnostics: grouped selections track individual leaves and duplicate origins"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual(
                "selection.cv",
                "import <native> using vendor::{used, unused} "
                "import <native> using vendor::used; fn f() { used(); }"
            );
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("selection"),
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:selections").value(),
                    .libraries = {},
                }
            );
            if (!expect(result.has_value())) {
                return;
            }
            auto count = 0uz;
            for (const auto& diagnostic : result->diagnostics) {
                if (diagnostic.finding.code == DiagnosticCode::LintUnusedImport) {
                    ++count;
                    if (!expect(diagnostic.attachment.primary.has_value())) {
                        return;
                    }
                    expect_equal(
                        sources.slice(diagnostic.attachment.primary->span),
                        std::string_view("unused")
                    );
                }
            }
            expect_equal(count, 1uz);
        };
});

} // namespace
