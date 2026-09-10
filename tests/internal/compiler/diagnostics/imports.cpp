module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.imports;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: unused imports are tracked per import declaration") {
    auto sources = SourceManager();
    const auto used_provider = *sources.append_virtual(
        "used-provider.cv",
        "export fn selected() -> i32 { return 1; }\n"
        "export fn selected_peer() -> i32 { return 3; }\n"
    );
    const auto unused_provider =
        *sources.append_virtual("unused-provider.cv", "export fn spare() -> i32 { return 2; }\n");
    const auto app = *sources.append_virtual(
        "app.cv",
        "import used_provider using { selected, selected_peer, };\n"
        "import unused_provider using spare;\n"
        "fn read() -> i32 { return selected(); }\n"
    );
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = used_provider,
            .module_path = *CanonicalModulePath::from_value("used_provider"),
        },
        CompilationModuleInput {
            .source_id = unused_provider,
            .module_path = *CanonicalModulePath::from_value("unused_provider"),
        },
        CompilationModuleInput {
            .source_id = app,
            .module_path = *CanonicalModulePath::from_value("app"),
        },
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:imports").value(),
        }
    );

    REQUIRE(result.has_value());
    const auto* unused = find_compiler_diagnostic(result->diagnostics, "CV-LINT-UNUSED-IMPORT");
    REQUIRE(unused != nullptr);
    REQUIRE(unused->attachment.primary.has_value());
    CHECK_EQ(unused->attachment.primary->span.source_id, app);
    CHECK_EQ(
        sources.slice(unused->attachment.primary->span),
        "import unused_provider using spare;"
    );
}

TEST_CASE("Compiler diagnostics: multiple wildcard providers remain ambiguous at use") {
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
        CompilationModuleInput {
            .source_id = first,
            .module_path = *CanonicalModulePath::from_value("first")
        },
        CompilationModuleInput {
            .source_id = second,
            .module_path = *CanonicalModulePath::from_value("second")
        },
        CompilationModuleInput {
            .source_id = app,
            .module_path = *CanonicalModulePath::from_value("app")
        },
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:imports").value(),
        }
    );

    REQUIRE_FALSE(result.has_value());
    REQUIRE_EQ(result.error().size(), 1u);
    const auto* ambiguous = find_compiler_diagnostic(result.error(), "CV-NAME-AMBIGUOUS");
    REQUIRE(ambiguous != nullptr);
    REQUIRE(ambiguous->attachment.primary.has_value());
    CHECK_EQ(sources.slice(ambiguous->attachment.primary->span), "value");
    CHECK_EQ(ambiguous->attachment.related.size(), 2u);
}
