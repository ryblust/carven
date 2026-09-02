module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.flow;

import :artifacts;
import :backend.generation.request;
import :compilation.request;
import :compiler.compile;
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

TEST_CASE("Compiler diagnostics: value try is one control-transfer boundary") {
    auto sources = SourceManager();
    const auto source = std::string(
        "fn invalid() {\n"
        "    while true {\n"
        "        let returned = try { return; 0 } catch { _ => 0, };\n"
        "        let broken = try { break; 0 } catch { _ => 0, };\n"
        "        let continued = try { continue; 0 } catch { _ => 0, };\n"
        "        break;\n"
        "    }\n"
        "}\n"
    );
    const auto source_id = *sources.append_virtual("value-try-transfer.cv", source);
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("value_try_transfer"),
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetGenerationRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:flow").value(),
        }
    );

    REQUIRE_FALSE(result.has_value());
    auto transfers = std::flat_set<std::string_view>();
    for (const auto& diagnostic : result.error()) {
        if (diagnostic.finding.code != "CV-FLOW-TRANSFER-VALUE-BRANCH") {
            continue;
        }
        REQUIRE(diagnostic.attachment.primary.has_value());
        transfers.insert(sources.slice(diagnostic.attachment.primary->span));
    }
    CHECK_EQ(transfers.size(), 3u);
    CHECK(transfers.contains("break"));
    CHECK(transfers.contains("continue"));
    CHECK(transfers.contains("return"));
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
        TargetGenerationRequest {
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
        TargetGenerationRequest {
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
