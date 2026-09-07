module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.warnings;

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

TEST_CASE("Compiler diagnostics: successful compilation retains warning location") {
    auto sources = SourceManager();
    const auto source = std::string(
        "fn value(parameter: i32) {\n"
        "    let unused = 1;\n"
        "    return;\n"
        "    let unreachable = 1;\n"
        "}\n"
    );
    const auto source_id = *sources.append_virtual("warning.cv", source);
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("warning"),
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:semantics").value(),
        }
    );

    REQUIRE(result.has_value());
    const auto* unreachable = find_compiler_diagnostic(result->diagnostics, "CV-FLOW-UNREACHABLE");
    REQUIRE(unreachable != nullptr);
    REQUIRE(unreachable->attachment.primary.has_value());
    CHECK_EQ(sources.location(unreachable->attachment.primary->span).line, 4u);
    CHECK_EQ(sources.slice(unreachable->attachment.primary->span), "let unreachable = 1;");

    const auto* unused = find_compiler_diagnostic(result->diagnostics, "CV-LINT-UNUSED-LOCAL");
    REQUIRE(unused != nullptr);
    REQUIRE(unused->attachment.primary.has_value());
    CHECK_EQ(sources.slice(unused->attachment.primary->span), "unused");

    const auto* parameter =
        find_compiler_diagnostic(result->diagnostics, "CV-LINT-UNUSED-PARAMETER");
    REQUIRE(parameter != nullptr);
    REQUIRE(parameter->attachment.primary.has_value());
    CHECK_EQ(sources.slice(parameter->attachment.primary->span), "parameter");
}

TEST_CASE("Compiler diagnostics: global references do not consume explicit C++ imports") {
    auto sources = SourceManager();
    const auto source_id =
        *sources.append_virtual("global.cv", "import <native> using value; fn f() { ::value(); }");
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("global"),
    };
    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1uz)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:global-unused").value(),
        }
    );
    REQUIRE(result.has_value());
    const auto* unused = find_compiler_diagnostic(result->diagnostics, "CV-LINT-UNUSED-IMPORT");
    REQUIRE(unused != nullptr);
    REQUIRE(unused->attachment.primary.has_value());
    CHECK_EQ(sources.slice(unused->attachment.primary->span), "value");
}

TEST_CASE(
    "Compiler diagnostics: grouped selections track individual leaves and duplicate origins"
) {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual(
        "selection.cv",
        "import <native> using vendor::{used, unused}; "
        "import <native> using vendor::used; fn f() { used(); }"
    );
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("selection"),
    };
    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:selections").value(),
        }
    );
    REQUIRE(result.has_value());
    auto count = 0uz;
    for (const auto& diagnostic : result->diagnostics) {
        if (diagnostic.finding.code == "CV-LINT-UNUSED-IMPORT") {
            ++count;
            REQUIRE(diagnostic.attachment.primary.has_value());
            CHECK_EQ(sources.slice(diagnostic.attachment.primary->span), "unused");
        }
    }
    CHECK_EQ(count, 1uz);
}
