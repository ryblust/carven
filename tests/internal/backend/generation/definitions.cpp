module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.definitions;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.decl;
import :backend.target.traversal;
import :backend.target;
import :semantic.semir.decl;
import :semantic.semir.program;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct Definitions final {
    std::flat_set<std::string> functions;
    std::size_t closures = 0uz;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
};

auto Definitions::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    if (const auto* function = std::get_if<TargetFunctionDecl>(&declaration)) {
        functions.emplace(function->name.components().back().spelling());
    }
    closures += std::holds_alternative<TargetOutOfClassMemberDefinition>(declaration);
    return true;
}

} // namespace

TEST_CASE("Generation: native definitions follow residual references and selected test roots") {
    const auto modes = std::array {TestGenerationMode::None, TestGenerationMode::RunnerHeader};
    for (const auto mode : modes) {
        CAPTURE(static_cast<int>(mode));
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(R"(
                private const fn compile_only(value: i32) -> i32 => value + 1;
                const answer = compile_only(40);
                private fn dead_left(value: i32) -> i32 {
                    if value <= 0 { return 0; }
                    return dead_right(value - 1);
                }
                private fn dead_right(value: i32) -> i32 {
                    if value <= 0 { return 0; }
                    return dead_left(value - 1);
                }
                private fn inactive() -> i32 => 0;
                private fn dead_closure_leaf() -> i32 => 1;
                private fn dead_factory() => []() => dead_closure_leaf();
                private fn leaf() -> i32 => 2;
                private fn middle() -> i32 => leaf();
                private fn via_value() -> i32 => 3;
                private fn closure_leaf() -> i32 => 4;
                private fn test_leaf() -> i32 => 5;
                private fn recursive(value: i32) -> i32 {
                    if value == 0 { return 1; }
                    return recursive(value - 1);
                }
                fn exposed() -> i32 {
                    let callback = via_value;
                    let closure = []() => closure_leaf();
                    return middle() + callback() + closure() + recursive(2);
                }
                fn folded() -> i32 => if true { answer } else { inactive() };
                test "selected root" { check(test_leaf() == 5); }
                const test "analysis root" { check(compile_only(1) == 2); }
            )"),
            {.test_mode = mode,
             .linkage_domain = *LinkageDomain::explicit_value("definition_selection")}
        );
        auto definitions = Definitions();
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            REQUIRE(traverse_target_unit(unit.sections(), definitions));
        }
        const auto absent = std::array<std::string_view, 6> {
            "compile_only",
            "dead_left",
            "dead_right",
            "inactive",
            "dead_closure_leaf",
            "dead_factory"
        };
        for (const auto entry : compilation.semantic().declarations().functions()) {
            const auto source = compilation.semantic().provenance().spelling(entry.value.name);
            const auto name = compilation.target().names().function_identifier(entry.id).spelling();
            CAPTURE(source);
            const auto expected = source == "test_leaf" ? mode != TestGenerationMode::None
                                                        : !std::ranges::contains(absent, source);
            CHECK(definitions.functions.contains(std::string(name)) == expected);
        }
        CHECK(definitions.closures == 1uz);
    }
}
