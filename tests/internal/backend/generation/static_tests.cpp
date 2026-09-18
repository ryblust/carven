module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.static_tests;

import :backend.generation.plan;
import :backend.generation.request;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Generation: only runtime tests enter module schedules") {
    for (const auto runtime : {false, true}) {
        CAPTURE(runtime);
        const auto source =
            std::string(
                R"(const {} const { var n = 1; ++n; } const test "static" { check(2 + 2 == 4); })"
            )
            + (runtime ? R"(test "runtime" { check(3 + 3 == 6); })" : "");
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(source),
            {.test_mode = TestGenerationMode::RunnerEntryPoint,
             .linkage_domain = *LinkageDomain::explicit_value("test-stages")}
        );
        auto tests = 0uz;
        for (const auto artifact : compilation.target().artifacts()) {
            if (const auto* module =
                    std::get_if<TargetModuleImplementationArtifact>(&artifact.value)) {
                tests += module->schedule.emitted_tests.size();
                for (const auto id : module->schedule.emitted_tests) {
                    CHECK(!compilation.semantic().tests().test(id).is_const);
                }
            }
        }
        CHECK(tests == (runtime ? 1uz : 0uz));
        for (const auto artifact : compilation.target().artifacts()) {
            if (const auto* runner = std::get_if<TargetTestRunnerHeaderArtifact>(&artifact.value)) {
                CHECK(runner->module_runners.size() == (runtime ? 1uz : 0uz));
            }
        }
    }
}
