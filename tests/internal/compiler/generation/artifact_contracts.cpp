module carven:test.internal.compiler.generation.artifact_contracts;

import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.report;
import :frontend.program.parse;
import :semantic.analyze;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :support.timing;
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
        "Compiler pipeline: composed and staged compilation preserve artifacts and output",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = sources.append_virtual(
                "pipeline.cv",
                "const { print(\"analysis\"); } export struct Value { value: i32, }"
            );
            ct::require(source.has_value());
            const auto module_path = CanonicalModulePath::from_value("pipeline");
            ct::require(module_path.has_value());
            const auto other_source =
                sources.append_virtual("other.cv", "export struct Other { value: i32, }");
            ct::require(other_source.has_value());
            const auto other_module = CanonicalModulePath::from_value("other");
            ct::require(other_module.has_value());
            const auto inputs = std::array {
                SourceModuleInput {.source_id = *source, .module_path = *module_path},
                SourceModuleInput {.source_id = *other_source, .module_path = *other_module},
            };
            const auto batch = SourceBatch {.modules = inputs};
            const auto request = TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:pipeline").value(),
            };
            auto composed_output = std::string();
            auto composed_stages = std::vector<TimingStage>();
            const auto composed_timings =
                TimingOutput([&](TimingStage stage,
                                 std::chrono::steady_clock::duration elapsed) noexcept {
                    ct::expect(elapsed >= std::chrono::steady_clock::duration::zero());
                    composed_stages.push_back(stage);
                });
            auto composed = compile(
                sources,
                batch,
                request,
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    composed_output += bytes;
                },
                composed_timings
            );
            auto staged_output = std::string();
            auto staged_stages = std::vector<TimingStage>();
            const auto staged_timings =
                TimingOutput([&](TimingStage stage, std::chrono::steady_clock::duration) noexcept {
                    staged_stages.push_back(stage);
                });
            auto syntax = parse_program(sources, batch, staged_timings);
            if (!ct::expect(composed.has_value()) || !ct::expect(syntax.has_value())) {
                return;
            }
            auto semantic = analyze(
                std::move(*syntax),
                [&](ExecutionOutputStream, std::string_view bytes) noexcept {
                    staged_output += bytes;
                },
                staged_timings
            );
            if (!ct::expect(semantic.has_value())) {
                return;
            }
            const auto staged = generate_artifacts(
                std::move(semantic->value),
                request,
                std::nullopt,
                staged_timings
            );
            ct::expect_equal(composed_output, std::string_view("analysis"));
            ct::expect_equal(staged_output, composed_output);
            ct::expect_equal(composed->diagnostics.size(), semantic->diagnostics.size());
            const auto expected_stages = std::vector {
                TimingStage::Lexing,
                TimingStage::Parsing,
                TimingStage::Lexing,
                TimingStage::Parsing,
                TimingStage::SemanticAnalysis,
                TimingStage::CppGeneration,
            };
            ct::expect(composed_stages == expected_stages);
            ct::expect(staged_stages == composed_stages);
            if (!ct::expect_equal(composed->value.entries().size(), staged.entries().size())) {
                return;
            }
            for (const auto& [left, right] :
                 std::views::zip(composed->value.entries(), staged.entries())) {
                ct::expect_equal(left.logical_path, right.logical_path);
                ct::expect_equal(left.role, right.role);
                ct::expect(left.source_mapping == right.source_mapping);
                ct::expect_equal(left.content, right.content);
            }
        }
    );

    ct::test("Compiler pipeline: failures report only entered stages", [] static noexcept {
        struct FailureCase final {
            std::string_view source;
            std::vector<TimingStage> stages;
        };
        const auto cases = std::array {
            FailureCase {.source = "\"unterminated", .stages = {TimingStage::Lexing}},
            FailureCase {
                .source = "fn broken( {",
                .stages = {TimingStage::Lexing, TimingStage::Parsing},
            },
            FailureCase {
                .source = "export fn bad() -> i32 { return missing; }",
                .stages =
                    {TimingStage::Lexing, TimingStage::Parsing, TimingStage::SemanticAnalysis},
            },
        };
        for (const auto& failure : cases) {
            ct::scenario(failure.source, [&] noexcept {
                auto sources = SourceManager();
                const auto source =
                    sources.append_virtual("invalid.cv", std::string(failure.source));
                ct::require(source.has_value());
                const auto module_path = CanonicalModulePath::from_value("invalid");
                ct::require(module_path.has_value());
                const auto input =
                    SourceModuleInput {.source_id = *source, .module_path = *module_path};
                auto stages = std::vector<TimingStage>();
                const auto result = compile(
                    sources,
                    SourceBatch {.modules = std::span(&input, 1)},
                    TargetPlanningRequest {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain = LinkageDomain::explicit_value("test:pipeline").value(),
                    },
                    {},
                    [&](TimingStage stage, std::chrono::steady_clock::duration) noexcept {
                        stages.push_back(stage);
                    }
                );
                ct::expect(!result.has_value());
                ct::expect(stages == failure.stages);
            });
        }
    });

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
