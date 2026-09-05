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
import std;

namespace {

auto find_diagnostic(std::span<const Diagnostic> diagnostics, std::string_view code) noexcept
    -> const Diagnostic* {
    const auto found =
        std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        });
    return found == diagnostics.end() ? nullptr : &*found;
}

struct ErrorExpectation final {
    std::string_view name;
    std::string_view source;
    std::string_view code;
    std::string_view primary_text;
};

} // namespace

TEST_CASE("Compiler diagnostics: value regions report transfers and missing results") {
    static constexpr auto cases = std::to_array<ErrorExpectation>({
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

    for (const auto& expectation : cases) {
        CAPTURE(expectation.name);
        auto sources = SourceManager();
        const auto source_id = *sources.append_virtual(
            std::format("{}.cv", expectation.name),
            std::string(expectation.source)
        );
        const auto input = CompilationModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("value_region_error"),
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:flow").value(),
            }
        );

        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), expectation.code);
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), expectation.primary_text);
    }
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
    const auto* unreachable = find_diagnostic(result->diagnostics, "CV-FLOW-UNREACHABLE");
    REQUIRE(unreachable != nullptr);
    REQUIRE(unreachable->attachment.primary.has_value());
    CHECK_EQ(sources.slice(unreachable->attachment.primary->span), "let unreachable = 2;");
    CHECK(find_diagnostic(result->diagnostics, "CV-TEST-CONDITION-TYPE") == nullptr);
}

TEST_CASE("Compiler diagnostics: covered match arms retain warning identity and span") {
    auto sources = SourceManager();
    const auto source = std::string(
        "private fn classify(value: bool) {\n"
        "    match value {\n"
        "        false => {},\n"
        "        true => {},\n"
        "        _ => {},\n"
        "    }\n"
        "}\n"
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
    const auto* warning = find_diagnostic(result->diagnostics, "CV-FLOW-UNREACHABLE-MATCH-ARM");
    REQUIRE(warning != nullptr);
    CHECK_EQ(warning->finding.severity, DiagnosticSeverity::Warning);
    REQUIRE(warning->attachment.primary.has_value());
    CHECK_EQ(sources.slice(warning->attachment.primary->span), "_");
}
