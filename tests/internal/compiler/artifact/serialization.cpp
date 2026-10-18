module carven:test.internal.compiler.artifact.serialization;

import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :compiler.analysis;
import :compiler.artifact;
import :diagnostics.report;
import :semantic.analysis.program.checked;
import :semantic.artifact;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

auto serialization_program(std::string_view text) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("serialization.cv", std::string(text));
    require(source.has_value());
    const auto input = SourceModuleInput {
        .source_id = *source,
        .module_path = *CanonicalModulePath::from_value("serialization"),
    };
    auto analysis = analyze_compilation(
        std::move(sources),
        SourceBatch {.modules = std::span(&input, 1uz)},
        {
            .output = {},
            .timings = {},
            .collect_source_observations = false,
            .capture_output = false,
            .host_effects = HostEffects::Deny,
        }
    );
    require(analysis.program() != nullptr).note([&]() noexcept {
        return render_diagnostics(analysis.diagnostics(), analysis.sources());
    });
    auto result = std::move(analysis).take_program();
    require(result.has_value());
    return std::move(*result);
}

auto compare_artifacts(
    const GeneratedArtifactSet& expected,
    const GeneratedArtifactSet& actual
) noexcept -> void {
    require_equal(expected.entries().size(), actual.entries().size());
    for (auto index = 0uz; index < expected.entries().size(); ++index) {
        const auto& before = expected.entries()[index];
        const auto& after = actual.entries()[index];
        expect_equal(after.logical_path, before.logical_path);
        expect_equal(after.role, before.role);
        expect_equal(after.source_path, before.source_path);
        expect_equal(after.content, before.content).note("artifact = ", before.logical_path);
    }
}

const TestSuite suite([] static noexcept {
    "Compilation artifact: ownership and cacheability preserve ordinary and test generation"_test =
        [] static noexcept {
            auto program = serialization_program(R"cv(
import std::async using cancel_point;
import std::async using yield_once;
async fn checked() { await cancel_point(); }
async fn yielding() { await yield_once(); }
async fn probe() { await yielding(); await checked(); }
test "runtime" { check(true); }
)cv");
            const auto unit = CheckedUnit {
                .program = std::move(program),
                .definitions = {},
                .cacheable = false,
            };
            constexpr auto ownership = std::array {
                CompilationModuleOwnership {.owned = true, .standard = false},
            };
            const auto encoded = encode_compilation_artifact(unit, ownership);
            require(encoded.has_value()).note([&]() noexcept { return encoded.error().message; });
            const auto artifact = read_compilation_artifact(*encoded);
            require(artifact.has_value());
            expect(!artifact->cacheable);
            require_equal(artifact->modules.size(), 1uz);
            expect(artifact->modules.front().owned);
            expect(!artifact->modules.front().standard);
            auto restored = decode_semir_program(artifact->program);
            require(restored.has_value()).note([&]() noexcept { return restored.error().message; });
            for (const auto mode :
                 {TestGenerationMode::None, TestGenerationMode::RunnerEntryPoint}) {
                const auto request = TargetPlanningRequest {
                    .test_mode = mode,
                    .linkage_domain = *LinkageDomain::explicit_value("test:semantic-artifact"),
                    .library = std::nullopt,
                };
                const auto expected = generate_artifacts(unit.program, request);
                require(expected.has_value());
                const auto actual = generate_artifacts(*restored, request);
                require(actual.has_value());
                compare_artifacts(*expected, *actual);
            }
        };

    "Semantic artifact: long operation chains preserve generation"_test = [] static noexcept {
        constexpr auto depth = 2048uz;
        auto text = std::string("fn deep(value: i32) -> i32 { return value");
        for (auto index = 0uz; index < depth; ++index) {
            text += " + value";
        }
        text += "; }";
        const auto program = serialization_program(text);
        const auto encoded = encode_semir_program(program);
        require(encoded.has_value()).note([&]() noexcept { return encoded.error().message; });
        auto restored = decode_semir_program(*encoded);
        require(restored.has_value()).note([&]() noexcept { return restored.error().message; });
        const auto request = TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("test:semantic-artifact"),
            .library = std::nullopt,
        };
        const auto expected = generate_artifacts(program, request);
        require(expected.has_value());
        const auto actual = generate_artifacts(*restored, request);
        require(actual.has_value());
        compare_artifacts(*expected, *actual);
    };
});

} // namespace
