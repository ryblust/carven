module carven:test.internal.compiler.diagnostics.modules;

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
        "Compiler diagnostics: module-scoped facts retain their owning source",
        [] static noexcept {
            auto sources = SourceManager();
            const auto healthy_source = *sources.append_virtual("healthy.cv", "fn healthy() {}\n");
            static constexpr auto failing_text = std::string_view(
                "struct Failure {}\n"
                "private fn caller() -> i32 { return failing(); }\n"
                "private fn failing() -> i32 { throw Failure {}; }\n"
            );
            const auto failing_source =
                *sources.append_virtual("failing.cv", std::string(failing_text));
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = healthy_source,
                    .module_path = *CanonicalModulePath::from_value("healthy"),
                },
                SourceModuleInput {
                    .source_id = failing_source,
                    .module_path = *CanonicalModulePath::from_value("failing"),
                },
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
                }
            );

            if (!ct::expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                ct::find_diagnostic(result.error(), DiagnosticCode::EffectUnmarked);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            if (!ct::expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            ct::expect((diagnostic->attachment.primary->span.source_id == failing_source));
            ct::expect_equal(
                sources.slice(diagnostic->attachment.primary->span),
                std::string_view("failing()")
            );
        }
    );

    ct::test(
        "Compiler diagnostics: explicit imports cannot bind two symbols to one name",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first =
                *sources.append_virtual("first.cv", "export fn value() -> i32 { return 1; }\n");
            const auto second =
                *sources.append_virtual("second.cv", "export fn value() -> i32 { return 2; }\n");
            const auto app = *sources.append_virtual(
                "app.cv",
                "import first using value;\n"
                "import second using value;\n"
                "fn read() -> i32 { return value(); }\n"
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
                    .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
                }
            );

            if (!ct::expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                ct::find_diagnostic(result.error(), DiagnosticCode::ImportResolution);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            if (!ct::expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            ct::expect((diagnostic->attachment.primary->span.source_id == app));
            ct::expect_equal(
                sources.slice(diagnostic->attachment.primary->span),
                std::string_view("value")
            );
        }
    );

    ct::test(
        "Compiler diagnostics: entry-point uniqueness spans source modules",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual("first.cv", "fn main() {}\n");
            const auto second = *sources.append_virtual("second.cv", "fn main() {}\n");
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = first,
                    .module_path = *CanonicalModulePath::from_value("first")
                },
                SourceModuleInput {
                    .source_id = second,
                    .module_path = *CanonicalModulePath::from_value("second")
                },
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
                }
            );

            if (!ct::expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                ct::find_diagnostic(result.error(), DiagnosticCode::EntryDuplicate);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            if (!ct::expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            if (!ct::expect_equal(diagnostic->attachment.related.size(), 1u)) {
                return;
            }
            ct::expect((diagnostic->attachment.primary->span.source_id == second));
            ct::expect((diagnostic->attachment.related.front().span.source_id == first));
            ct::expect_equal(
                sources.slice(diagnostic->attachment.primary->span),
                std::string_view("main")
            );
        }
    );

    ct::test(
        "Compiler diagnostics: recursive storage across modules is diagnosed semantically",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual(
                "a.cv",
                "import b using B;\nexport struct A { value: B }\n"
            );
            const auto second = *sources.append_virtual(
                "b.cv",
                "import a using A;\nexport struct B { value: A }\n"
            );
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = first,
                    .module_path = *CanonicalModulePath::from_value("a")
                },
                SourceModuleInput {
                    .source_id = second,
                    .module_path = *CanonicalModulePath::from_value("b")
                },
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:modules").value(),
                }
            );

            if (!ct::expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                ct::find_diagnostic(result.error(), DiagnosticCode::TypeRecursiveStorage);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            if (!ct::expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            ct::expect(
                (diagnostic->attachment.primary->span.source_id == first
                 || diagnostic->attachment.primary->span.source_id == second)
            );
        }
    );

    ct::test(
        "Compiler diagnostics: implicit entry locals remain local to their body",
        [] static noexcept {
            const auto cases = std::array {
                CompilerErrorExpectation {
                    .name = "module function cannot capture entry local",
                    .source = "let local = 42; fn read() => local;",
                    .code = DiagnosticCode::NameUnresolved,
                    .primary_text = "local",
                },
                CompilerErrorExpectation {
                    .name = "implicit entry has no source name binding",
                    .source = "main();",
                    .code = DiagnosticCode::NameUnresolved,
                    .primary_text = "main",
                },
                CompilerErrorExpectation {
                    .name = "explicit and implicit entry conflict",
                    .source = "fn main() {} println(42);",
                    .code = DiagnosticCode::EntryDuplicate,
                    .primary_text = "println(42);",
                },
            };
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Compiler diagnostics: each file's top-level body counts as an entry",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual("first.cv", "println(1);");
            const auto second = *sources.append_virtual("second.cv", "println(2);");
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = first,
                    .module_path = *CanonicalModulePath::from_value("first")
                },
                SourceModuleInput {
                    .source_id = second,
                    .module_path = *CanonicalModulePath::from_value("second")
                },
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = inputs},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = *LinkageDomain::explicit_value("test:top-level"),
                }
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                ct::find_diagnostic(result.error(), DiagnosticCode::EntryDuplicate);
            if (!ct::expect(diagnostic != nullptr)) {
                return;
            }
            if (!ct::expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            ct::expect((diagnostic->attachment.primary->span.source_id == second));
            if (!ct::expect_equal(diagnostic->attachment.related.size(), 1uz)) {
                return;
            }
            ct::expect((diagnostic->attachment.related.front().span.source_id == first));
        }
    );

    ct::test("Compiler: top-level bodies use ordinary callable analysis", [] static noexcept {
        auto sources = SourceManager();
        const auto source = *sources.append_virtual("app.cv", R"(
        let result = twice(21);
        fn twice(value: i32) => value * 2;
        const expected = 42;
        var total = 0;
        for index in 0..3 { total += index; }
        println(result, expected, total);
    )");
        const auto input = SourceModuleInput {
            .source_id = source,
            .module_path = *CanonicalModulePath::from_value("app")
        };
        const auto result = compile(
            sources,
            SourceBatch {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = *LinkageDomain::explicit_value("test:top-level"),
            }
        );
        ct::expect(result.has_value());
    });
});

} // namespace
