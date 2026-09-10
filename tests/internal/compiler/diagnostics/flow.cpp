module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.flow;

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

TEST_CASE("Compiler diagnostics: value regions report transfers and missing results") {
    static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {
            .name = "value if return",
            .source = "fn invalid() { let value = if true { return; 0 } else { 1 }; }",
            .code = "CV-FLOW-TRANSFER-VALUE-BRANCH",
            .primary_text = "return",
        },
        {
            .name = "value match break",
            .source = "fn invalid() { while true { "
                      "let value = match 0 { _ => { break; 0 }, }; break; } }",
            .code = "CV-FLOW-TRANSFER-VALUE-BRANCH",
            .primary_text = "break",
        },
        {
            .name = "value try continue",
            .source = "fn invalid() { while true { "
                      "let value = try { continue; 0 } catch { _ => 0, }; break; } }",
            .code = "CV-FLOW-TRANSFER-VALUE-BRANCH",
            .primary_text = "continue",
        },
        {
            .name = "value try missing normal result",
            .source = "fn invalid() { "
                      "let value = try { let side_effect = 0; } catch { _ => 1, }; }",
            .code = "CV-FLOW-VALUE-BRANCH-RESULT",
            .primary_text = "{ let side_effect = 0; }",
        },
    });

    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: inline-test whole-test transfer controls reachability") {
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
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("test_flow"),
    };
    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:flow").value(),
        }
    );

    REQUIRE(result.has_value());
    const auto* unreachable = find_compiler_diagnostic(result->diagnostics, "CV-FLOW-UNREACHABLE");
    REQUIRE(unreachable != nullptr);
    REQUIRE(unreachable->attachment.primary.has_value());
    CHECK_EQ(sources.slice(unreachable->attachment.primary->span), "let unreachable = 2;");
    CHECK(find_compiler_diagnostic(result->diagnostics, "CV-TEST-CONDITION-TYPE") == nullptr);
}

TEST_CASE("Compiler diagnostics: covered match arms retain warning identity and span") {
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
    for (const auto& entry : cases) {
        CAPTURE(entry.type);
        CAPTURE(entry.patterns);
        auto sources = SourceManager();
        const auto source = std::format(
            "private fn classify(value: {}) {{ match value {{ {} }} }}",
            entry.type,
            entry.patterns
        );
        const auto source_id = *sources.append_virtual("covered-match-arm.cv", source);
        const auto input = CompilationModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("covered_match_arm"),
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:flow").value(),
            }
        );

        REQUIRE(result.has_value());
        const auto* warning =
            find_compiler_diagnostic(result->diagnostics, "CV-FLOW-UNREACHABLE-MATCH-ARM");
        REQUIRE(warning != nullptr);
        CHECK_EQ(warning->finding.severity, DiagnosticSeverity::Warning);
        REQUIRE(warning->attachment.primary.has_value());
        CHECK_EQ(sources.slice(warning->attachment.primary->span), entry.covered);
    }
}
