module carven:test.internal.compiler.diagnostics.flow;

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
    "Compiler diagnostics: value regions report transfers and missing results"_test =
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {
                    .name = "value if return",
                    .source = "fn invalid() { let value = if true { return; 0 } else { 1 }; }",
                    .code = DiagnosticCode::FlowTransferBoundary,
                    .primary_text = "return",
                },
                {
                    .name = "value match break",
                    .source = "fn invalid() { while true { "
                              "let value = match 0 { _ => { break; 0 }, }; break; } }",
                    .code = DiagnosticCode::FlowTransferBoundary,
                    .primary_text = "break",
                },
                {
                    .name = "value try continue",
                    .source = "fn invalid() { while true { "
                              "let value = try { continue; 0 } catch { _ => 0, }; break; } }",
                    .code = DiagnosticCode::FlowTransferBoundary,
                    .primary_text = "continue",
                },
                {
                    .name = "value try missing normal result",
                    .source = "fn invalid() { "
                              "let value = try { let side_effect = 0; } catch { _ => 1, }; }",
                    .code = DiagnosticCode::FlowValueBranchResult,
                    .primary_text = "{ let side_effect = 0; }",
                },
            });

            check_compiler_errors(cases);
        };

    "Compiler diagnostics: inline-test whole-test transfer controls reachability"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = std::string(
                "test \"flow\" {\n"
                "    require(true);\n"
                "    let reachable = 1;\n"
                "    fail();\n"
                "    let unreachable = 2;\n"
                "}\n"
            );
            const auto source_id = *sources.append_virtual("test-flow.cv", source);
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("test_flow"),
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:flow").value(),
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
            expect_equal(
                sources.slice(unreachable->attachment.primary->span),
                std::string_view("let unreachable = 2;")
            );
            expect_no_diagnostic(result->diagnostics, DiagnosticCode::TestConditionType);
        };

    "Compiler diagnostics: covered match arms retain warning identity and span"_test =
        [] static noexcept {
            struct Case final {
                std::string_view type;
                std::string_view patterns;
                std::string_view covered;
            };

            const auto cases = std::to_array<Case>({
                {.type = "bool", .patterns = "false => {}, true => {}, _ => {},", .covered = "_"},
                {.type = "f32", .patterns = "0.0 => {}, -0.0 => {}, _ => {},", .covered = "-0.0"},
                {.type = "f32", .patterns = "-0.0 => {}, 0.0 => {}, _ => {},", .covered = "0.0"},
                {.type = "f64", .patterns = "0.0 => {}, -0.0 => {}, _ => {},", .covered = "-0.0"},
                {.type = "f64", .patterns = "-0.0 => {}, 0.0 => {}, _ => {},", .covered = "0.0"},
            });
            each(
                cases,
                [](const Case& entry) static noexcept {
                    return std::format("{}: {}", entry.type, entry.patterns);
                },
                [&](const Case& entry) noexcept {
                    auto sources = SourceManager();
                    const auto source = std::format(
                        "private fn classify(value: {}) {{ match value {{ {} }} }}",
                        entry.type,
                        entry.patterns
                    );
                    const auto source_id = *sources.append_virtual("covered-match-arm.cv", source);
                    const auto input = SourceModuleInput {
                        .source_id = source_id,
                        .module_path = *CanonicalModulePath::from_value("covered_match_arm"),
                    };
                    const auto result = compile(
                        sources,
                        SourceBatch {.modules = std::span(&input, 1)},
                        TargetPlanningRequest {
                            .test_mode = TestGenerationMode::None,
                            .linkage_domain = LinkageDomain::explicit_value("test:flow").value(),
                        }
                    );

                    if (!(expect(result.has_value()))) {
                        return;
                    }
                    const auto* warning = find_diagnostic(
                        result->diagnostics,
                        DiagnosticCode::FlowUnreachableMatchArm
                    );
                    if (!(expect(warning != nullptr))) {
                        return;
                    }
                    expect_equal(warning->finding.severity, DiagnosticSeverity::Warning);
                    if (!(expect(warning->attachment.primary.has_value()))) {
                        return;
                    }
                    expect_equal(sources.slice(warning->attachment.primary->span), entry.covered);
                }
            );
        };
});

} // namespace
