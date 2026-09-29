module carven:test.internal.compiler.generation.artifact_contracts;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.report;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

template<typename Result>
auto checked_artifacts(Result result, const SourceManager& sources) noexcept
    -> GeneratedArtifactSet {
    ct::require(result.has_value()).note([&] noexcept {
        return render_diagnostics(result.error(), sources);
    });
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
    ct::require(source.has_value());
    const auto path = CanonicalModulePath::from_value("dependencies");
    ct::require(path.has_value());
    const auto input = SourceModuleInput {.source_id = *source, .module_path = *path};
    auto result = compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:artifacts").value(),
        }
    );
    return checked_artifacts(std::move(result), sources);
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
    ct::require(source.has_value());
    const auto path = CanonicalModulePath::from_value("opaque");
    ct::require(path.has_value());
    const auto input = SourceModuleInput {.source_id = *source, .module_path = *path};
    auto result = compile(
        sources,
        SourceBatch {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:artifacts").value(),
        }
    );
    return checked_artifacts(std::move(result), sources);
}

auto artifact_content(const GeneratedArtifactSet& artifacts, std::string_view path) noexcept
    -> std::string_view {
    const auto found =
        std::ranges::find(artifacts.entries(), path, &GeneratedArtifact::logical_path);
    ct::require(found != artifacts.entries().end()).note("artifact:", path);
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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generated artifacts: artifacts retain source attribution and exact dependencies",
        [] static noexcept {
            const auto artifacts = compile_dependency_fixture();
            const auto implementation = artifact_content(artifacts, "dependencies.cpp");

            ct::expect(implementation.contains("\"dependencies.cv\""));
            ct::expect(!(implementation.contains("#include <carven/runtime/runtime.hpp>")));
            ct::expect(implementation.contains("#include <carven/runtime/array.hpp>"));
            ct::expect(implementation.contains("#include <carven/runtime/callable.hpp>"));
            ct::expect(implementation.contains("#include <carven/runtime/numeric.hpp>"));
            ct::expect(implementation.contains("#include <carven/runtime/outcome.hpp>"));
            ct::expect(!(implementation.contains("#include <carven/runtime/entry.hpp>")));
            ct::expect(!(implementation.contains("#include <carven/runtime/text.hpp>")));
            ct::expect(implementation.contains("#include \"dependency_provider.hpp\""));
        }
    );

    ct::test(
        "Generated artifacts: raw fragments preserve line-marker-shaped bytes",
        [] static noexcept {
            const auto artifacts = compile_opaque_raw_fixture();
            const auto implementation = artifact_content(artifacts, "opaque.cpp");

            ct::expect(implementation.contains("R\"(#line CARVEN_SOURCE_LINE 7 \\\"raw.cv\\\")\""));
            ct::expect(
                implementation.contains("R\"(#line CARVEN_GENERATED_LINE \\\"raw.cpp\\\")\"")
            );
            ct::expect(
                implementation.find("cv_raw_source") < implementation.find("cv_raw_generated")
            );
            ct::expect_greater_equal(occurrence_count(implementation, "\"opaque.cv\""), 2u);
        }
    );
});

} // namespace
