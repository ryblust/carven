module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.modules;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: module-scoped facts retain their owning source") {
    auto sources = SourceManager();
    const auto healthy_source = *sources.append_virtual("healthy.cv", "fn healthy() {}\n");
    static constexpr auto failing_text = std::string_view(
        "struct Failure {}\n"
        "private fn caller() -> i32 { return failing(); }\n"
        "private fn failing() -> i32 { throw Failure {}; }\n"
    );
    const auto failing_source = *sources.append_virtual("failing.cv", std::string(failing_text));
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

    REQUIRE(!result.has_value());
    const auto* diagnostic = find_compiler_diagnostic(result.error(), "CV-EFFECT-UNMARKED");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->attachment.primary.has_value());
    CHECK_EQ(diagnostic->attachment.primary->span.source_id, failing_source);
    CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "failing()");
}

TEST_CASE("Compiler diagnostics: module graph errors use one catalog identity space") {
    SUBCASE("explicit imports cannot bind two symbols to one name") {
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

        REQUIRE(!result.has_value());
        const auto* diagnostic = find_compiler_diagnostic(result.error(), "CV-IMPORT-RESOLUTION");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(diagnostic->attachment.primary->span.source_id, app);
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "value");
    }

    SUBCASE("entry-point uniqueness spans source modules") {
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

        REQUIRE(!result.has_value());
        const auto* diagnostic = find_compiler_diagnostic(result.error(), "CV-ENTRY-DUPLICATE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        REQUIRE_EQ(diagnostic->attachment.related.size(), 1u);
        CHECK_EQ(diagnostic->attachment.primary->span.source_id, second);
        CHECK_EQ(diagnostic->attachment.related.front().span.source_id, first);
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "main");
    }

    SUBCASE("recursive storage across modules is diagnosed semantically") {
        auto sources = SourceManager();
        const auto first =
            *sources.append_virtual("a.cv", "import b using B;\nexport struct A { value: B }\n");
        const auto second =
            *sources.append_virtual("b.cv", "import a using A;\nexport struct B { value: A }\n");
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

        REQUIRE(!result.has_value());
        const auto* diagnostic =
            find_compiler_diagnostic(result.error(), "CV-TYPE-RECURSIVE-STORAGE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK(
            (diagnostic->attachment.primary->span.source_id == first
             || diagnostic->attachment.primary->span.source_id == second)
        );
    }
}

TEST_CASE("Compiler diagnostics: implicit entry locals remain local to their body") {
    const auto cases = std::array {
        CompilerErrorExpectation {
            .name = "module function cannot capture entry local",
            .source = "let local = 42; fn read() => local;",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "local",
        },
        CompilerErrorExpectation {
            .name = "implicit entry has no source name binding",
            .source = "main();",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "main",
        },
        CompilerErrorExpectation {
            .name = "explicit and implicit entry conflict",
            .source = "fn main() {} println(42);",
            .code = "CV-ENTRY-DUPLICATE",
            .primary_text = "println(42);",
        },
    };
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: each file's top-level body counts as an entry") {
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
    REQUIRE(!result.has_value());
    const auto* diagnostic = find_compiler_diagnostic(result.error(), "CV-ENTRY-DUPLICATE");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->attachment.primary.has_value());
    CHECK_EQ(diagnostic->attachment.primary->span.source_id, second);
    REQUIRE_EQ(diagnostic->attachment.related.size(), 1uz);
    CHECK_EQ(diagnostic->attachment.related.front().span.source_id, first);
}

TEST_CASE("Compiler: top-level bodies use ordinary callable analysis") {
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
    REQUIRE(result.has_value());
}
