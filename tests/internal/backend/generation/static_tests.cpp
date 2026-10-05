module carven:test.internal.backend.generation.static_tests;

import :backend.generation.plan;
import :backend.generation.request;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generation: only runtime tests enter module schedules"_test = [] static noexcept {
        for (const auto runtime : {false, true}) {
            const auto source =
                std::string(
                    R"(const "phase" {} const "" {} const "phase" { var n = 1; ++n; }
                   const test { check(2 + 2 == 4); }
                   const test "" { check(true); })"
                )
                + (runtime ? R"(test {} test {} test "runtime" {})" : "");
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(source),
                {.test_mode = TestGenerationMode::RunnerEntryPoint,
                 .linkage_domain = *LinkageDomain::explicit_value("test-stages")}
            );
            auto tests = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                if (const auto* module_artifact =
                        std::get_if<TargetModuleImplementationArtifact>(&artifact.value)) {
                    tests += module_artifact->schedule.emitted_tests.size();
                    for (const auto id : module_artifact->schedule.emitted_tests) {
                        expect(!compilation.semantic().tests().test(id).is_const)
                            .note("runtime: ", runtime);
                    }
                }
            }
            expect(tests == (runtime ? 3uz : 0uz)).note("runtime: ", runtime);
            for (const auto artifact : compilation.target().artifacts()) {
                if (const auto* runner =
                        std::get_if<TargetTestRunnerHeaderArtifact>(&artifact.value)) {
                    expect(runner->module_runners.size() == (runtime ? 1uz : 0uz))
                        .note("runtime: ", runtime);
                }
            }
        }
    };
});

} // namespace
