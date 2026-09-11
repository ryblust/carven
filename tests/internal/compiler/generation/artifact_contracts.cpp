module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.generation.artifact_contracts;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.code;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

template<typename Result>
auto checked_artifacts(Result result) noexcept -> GeneratedArtifactSet {
    auto diagnostic_report = std::string();
    if (!result.has_value()) {
        for (const auto& diagnostic : result.error()) {
            diagnostic_report += std::format(
                "{}: {}\n",
                diagnostic_code_info(diagnostic.finding.code).name,
                diagnostic.finding.message
            );
        }
    }
    INFO(diagnostic_report);
    REQUIRE(result.has_value());
    if (!result.has_value()) {
        return GeneratedArtifactSet(std::vector<GeneratedArtifact> {});
    }
    return std::move(result->value);
}

auto compile_dependency_fixture() noexcept -> GeneratedArtifactSet {
    auto sources = SourceManager();
    const auto source = sources.append_virtual(
        "dependencies.cv",
        R"(import "dependency_provider.hpp";

private import(cpp) fn observe(value: i32) -> i32;
enum Failure { Invalid, }

fn indexed(values: [i32; 2], index: i32) -> i32 {
    return observe(values[index] + 1);
}

fn invoke(callback: fn(i32) -> i32) -> i32 {
    return callback(1);
}

fn fail_value() -> i32 throw Failure {
    throw Failure::Invalid;
}
)"
    );
    REQUIRE(source.has_value());
    const auto path = CanonicalModulePath::from_value("dependencies");
    REQUIRE(path.has_value());
    const auto input = CompilationModuleInput {.source_id = *source, .module_path = *path};
    auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:artifacts").value(),
        }
    );
    return checked_artifacts(std::move(result));
}

auto compile_opaque_raw_fixture() noexcept -> GeneratedArtifactSet {
    auto sources = SourceManager();
    const auto source = sources.append_virtual(
        "opaque.cv",
        "#[cpp] ---\n"
        "inline constexpr auto cv_raw_source = R\"(#line CARVEN_SOURCE_LINE 7 \\\"raw.cv\\\")\";\n"
        "---\n"
        "\n"
        "\n"
        "#[cpp] -----\n"
        "inline constexpr auto cv_raw_generated = R\"(#line CARVEN_GENERATED_LINE \\\"raw.cpp\\\")\";\n"
        "-----\n"
    );
    REQUIRE(source.has_value());
    const auto path = CanonicalModulePath::from_value("opaque");
    REQUIRE(path.has_value());
    const auto input = CompilationModuleInput {.source_id = *source, .module_path = *path};
    auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:artifacts").value(),
        }
    );
    return checked_artifacts(std::move(result));
}

auto artifact_content(const GeneratedArtifactSet& artifacts, std::string_view path) noexcept
    -> std::string_view {
    const auto found =
        std::ranges::find(artifacts.entries(), path, &GeneratedArtifact::logical_path);
    REQUIRE(found != artifacts.entries().end());
    if (found == artifacts.entries().end()) {
        return {};
    }
    return found->content;
}

auto occurrence_count(std::string_view text, std::string_view needle) noexcept -> std::size_t {
    auto count = 0uz;
    for (auto position = text.find(needle); position != std::string_view::npos;
         position = text.find(needle, position + needle.size())) {
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE("Generated artifacts: artifacts retain source attribution and exact dependencies") {
    const auto artifacts = compile_dependency_fixture();
    const auto implementation = artifact_content(artifacts, "dependencies.cpp");

    CHECK(implementation.contains("\"dependencies.cv\""));
    CHECK_FALSE(implementation.contains("#include <carven/runtime/runtime.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/array.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/callable.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/numeric.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/outcome.hpp>"));
    CHECK_FALSE(implementation.contains("#include <carven/runtime/entry.hpp>"));
    CHECK_FALSE(implementation.contains("#include <carven/runtime/text.hpp>"));
    CHECK(implementation.contains("#include \"dependency_provider.hpp\""));
}

TEST_CASE("Generated artifacts: raw fragments preserve line-marker-shaped bytes") {
    const auto artifacts = compile_opaque_raw_fixture();
    const auto implementation = artifact_content(artifacts, "opaque.cpp");

    CHECK(implementation.contains("R\"(#line CARVEN_SOURCE_LINE 7 \\\"raw.cv\\\")\""));
    CHECK(implementation.contains("R\"(#line CARVEN_GENERATED_LINE \\\"raw.cpp\\\")\""));
    CHECK(implementation.find("cv_raw_source") < implementation.find("cv_raw_generated"));
    CHECK_GE(occurrence_count(implementation, "\"opaque.cv\""), 2u);
}
