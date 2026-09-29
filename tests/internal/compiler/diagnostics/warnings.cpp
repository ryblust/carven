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
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler diagnostics: successful compilation retains warning location",
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
                }
            );

            if (!ct::expect(result.has_value())) {
                return;
            }
            const auto* unreachable =
                ct::find_diagnostic(result->diagnostics, DiagnosticCode::FlowUnreachable);
            if (!ct::expect(unreachable != nullptr)) {
                return;
            }
            if (!ct::expect(unreachable->attachment.primary.has_value())) {
                return;
            }
            ct::expect_equal(sources.location(unreachable->attachment.primary->span).line, 4u);
            ct::expect_equal(
                sources.slice(unreachable->attachment.primary->span),
                std::string_view("let unreachable = 1;")
            );

            const auto* unused =
                ct::find_diagnostic(result->diagnostics, DiagnosticCode::LintUnusedLocal);
            if (!ct::expect(unused != nullptr)) {
                return;
            }
            if (!ct::expect(unused->attachment.primary.has_value())) {
                return;
            }
            ct::expect_equal(
                sources.slice(unused->attachment.primary->span),
                std::string_view("unused")
            );

            const auto* parameter =
                ct::find_diagnostic(result->diagnostics, DiagnosticCode::LintUnusedParameter);
            if (!ct::expect(parameter != nullptr)) {
                return;
            }
            if (!ct::expect(parameter->attachment.primary.has_value())) {
                return;
            }
            ct::expect_equal(
                sources.slice(parameter->attachment.primary->span),
                std::string_view("parameter")
            );
        }
    );

    ct::test(
        "Compiler diagnostics: global references do not consume explicit C++ imports",
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
                }
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            const auto* unused =
                ct::find_diagnostic(result->diagnostics, DiagnosticCode::LintUnusedImport);
            if (!ct::expect(unused != nullptr)) {
                return;
            }
            if (!ct::expect(unused->attachment.primary.has_value())) {
                return;
            }
            ct::expect_equal(
                sources.slice(unused->attachment.primary->span),
                std::string_view("value")
            );
        }
    );

    ct::test(
        "Compiler diagnostics: grouped selections track individual leaves and duplicate origins",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual(
                "selection.cv",
                "import <native> using vendor::{used, unused}; "
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
                }
            );
            if (!ct::expect(result.has_value())) {
                return;
            }
            auto count = 0uz;
            for (const auto& diagnostic : result->diagnostics) {
                if (diagnostic.finding.code == DiagnosticCode::LintUnusedImport) {
                    ++count;
                    if (!ct::expect(diagnostic.attachment.primary.has_value())) {
                        return;
                    }
                    ct::expect_equal(
                        sources.slice(diagnostic.attachment.primary->span),
                        std::string_view("unused")
                    );
                }
            }
            ct::expect_equal(count, 1uz);
        }
    );

    ct::test(
        "Compiler diagnostics: returned owners that could transfer report a copy",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = std::string(
                "struct Named { name: String, id: i32 }\n"
                "fn take_copy(&&taken_text: String) -> String => taken_text;\n"
                "fn take_moved(&&text: String) -> String => &&text;\n"
                "fn local() -> String { let built: String = \"a\"; return built; }\n"
                "fn record() -> Named { let named_value = Named { name: \"n\", id: 1 }; return named_value; }\n"
                "fn borrowed() -> String {\n"
                "    let owner: String = \"a\";\n"
                "    let view = owner.as_str();\n"
                "    let length = view.len();\n"
                "    return owner;\n"
                "}\n"
                "fn read_parameter(text: String) -> String => text;\n"
                "fn scalar() -> i32 { let n = 3; return n; }\n"
                "fn empty() -> [String; 0] { let empty: [String; 0] = []; return empty; }\n"
                "fn nested_empty() -> [[String; 0]; 1] { let empty: [[String; 0]; 1] = [[]]; return empty; }\n"
                "fn array() -> [String; 1] { let texts: [String; 1] = [\"x\"]; return texts; }\n"
            );
            const auto source_id = *sources.append_virtual("return_copy.cv", source);
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("return_copy"),
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:semantics").value(),
                }
            );

            if (!ct::expect(result.has_value())) {
                return;
            }
            auto reported = std::vector<std::string_view>();
            for (const auto& diagnostic : result->diagnostics) {
                if (diagnostic.finding.code == DiagnosticCode::LintReturnCopy) {
                    if (!ct::expect(diagnostic.attachment.primary.has_value())) {
                        return;
                    }
                    reported.push_back(sources.slice(diagnostic.attachment.primary->span));
                }
            }
            std::ranges::sort(reported);
            // A live borrow, Read parameters and scalar copies are not transferable
            // copies worth reporting; explicit Take is already a transfer.
            ct::expect(
                (reported
                 == std::vector<std::string_view> {"built", "named_value", "taken_text", "texts"})
            );
        }
    );
});

} // namespace
