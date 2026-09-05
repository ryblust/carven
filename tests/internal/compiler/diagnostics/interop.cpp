module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.interop;

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

using compiler_diagnostics_test::ErrorExpectation;
using compiler_diagnostics_test::check_errors;
using compiler_diagnostics_test::find_diagnostic;

TEST_CASE("Compiler diagnostics: interop failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<ErrorExpectation>({
        {
            .name = "unsupported import(cpp) boundary type",
            .source = "private import(cpp) fn invalid(value: str);",
            .code = "CV-CPP-CARRIER",
            .primary_text = "value: str",
        },
        {
            .name = "unsupported export(cpp) boundary type",
            .source = "export(cpp) fn invalid(value: str) {}",
            .code = "CV-CPP-CARRIER",
            .primary_text = "value: str",
        },
        {
            .name = "C++ boundary access mode",
            .source = "private import(cpp) fn invalid(&value: i32);",
            .code = "CV-CPP-BOUNDARY",
            .primary_text = "&",
        },
        {
            .name = "C++ boundary failure contract",
            .source = "struct Failure {} private import(cpp) fn invalid() throw Failure;",
            .code = "CV-CPP-BOUNDARY",
            .primary_text = "throw Failure",
        },
        {
            .name = "unrepresentable std provider name",
            .source = "private import(cpp) fn std() -> i32;",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "std",
        },
        {
            .name = "unrepresentable carven provider name",
            .source = "private import(cpp) fn carven() -> i32;",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "carven",
        },
        {
            .name = "unrepresentable main provider name",
            .source = "private import(cpp) fn main() -> i32;",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "main",
        },
        {
            .name = "C++ keyword API name",
            .source = "export(cpp) fn class() -> i32 { return 1; }",
            .code = "CV-CPP-IDENTIFIER",
            .primary_text = "class",
        },
    });
    check_errors(cases);
}

TEST_CASE("Compiler diagnostics: C++ API namespace collisions are Carven-owned") {
    auto sources = SourceManager();
    const auto parent_source =
        *sources.append_virtual("surface.cv", "export(cpp) fn nested() -> i32 { return 1; }");
    const auto child_source =
        *sources.append_virtual("nested.cv", "export(cpp) fn value() -> i32 { return 2; }");
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = parent_source,
            .module_path = *CanonicalModulePath::from_value("surface"),
        },
        CompilationModuleInput {
            .source_id = child_source,
            .module_path = *CanonicalModulePath::from_value("surface.nested"),
        },
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:cpp-collision").value(),
        }
    );

    REQUIRE(!result.has_value());
    const auto* collision = find_diagnostic(result.error(), "CV-CPP-API-PATH-COLLISION");
    REQUIRE(collision != nullptr);
    REQUIRE(collision->attachment.primary.has_value());
    CHECK_EQ(sources.slice(collision->attachment.primary->span), "export(cpp)");
}


TEST_CASE("Compiler diagnostics: an exported module path is validated once") {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual(
        "class.cv",
        "export(cpp) fn first() -> i32 { return 1; }\n"
        "export(cpp) fn second() -> i32 { return 2; }\n"
    );
    const auto module_path = CanonicalModulePath::from_value("class");
    REQUIRE(module_path.has_value());
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *module_path,
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:cpp-module-path").value(),
        }
    );

    REQUIRE(!result.has_value());
    const auto count = std::ranges::count_if(result.error(), [](const Diagnostic& diagnostic) {
        return diagnostic.finding.code == "CV-CPP-IDENTIFIER";
    });
    CHECK_EQ(count, 1);
    const auto* diagnostic = find_diagnostic(result.error(), "CV-CPP-IDENTIFIER");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->attachment.primary.has_value());
    CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "export(cpp)");
}
