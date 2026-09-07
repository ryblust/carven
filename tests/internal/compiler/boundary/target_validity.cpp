module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.boundary.target_validity;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

TEST_CASE("Compiler integration: C++ target validity remains downstream-owned") {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual(
        "native.cv",
        "#[cpp] ---\n"
        "auto invalid = object.virtual;\n"
        "---\n"
        "fn value() {}\n"
    );
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("native"),
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:target-validity").value(),
        }
    );

    if (!result.has_value()) {
        for (const auto& diagnostic : result.error()) {
            INFO(diagnostic.finding.message);
        }
    }
    REQUIRE(result.has_value());
    REQUIRE_EQ(result->value.entries().size(), 2u);
    CHECK(result->value.entries()[1].content.contains("auto invalid = object.virtual;"));
}
