module carven:test.internal.semantic.analysis.match_selection;

import :semantic.analysis.coverage;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Match selection: rejection facts follow the remaining domain", [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            std::vector<bool> rejection;
        };
        const auto cases = std::array {
            Case {
                .name = "boolean complement",
                .source = "fn select(value: bool) -> i32 => match value { "
                          "true => 1, false => 2, };",
                .rejection = {true, false},
            },
            Case {
                .name = "guarded current pattern",
                .source = "fn select(value: bool, accept: bool) -> i32 => match value { "
                          "true => 1, false if accept => 2, false => 3, };",
                .rejection = {true, false, false},
            },
            Case {
                .name = "rejected guard restores accepted values",
                .source = "fn select(value: bool, accept: bool) -> i32 => match value { "
                          "true if accept => 1, false => 2, true => 3, };",
                .rejection = {true, true, false},
            },
            Case {
                .name = "whole-domain alternatives",
                .source = "fn select(value: bool) -> i32 => match value { "
                          "true | false => 1, };",
                .rejection = {false},
            },
            Case {
                .name = "integer interval complement",
                .source = "fn select(value: i32) -> i32 => match value { "
                          "..0 => 1, 0.. => 2, };",
                .rejection = {true, false},
            },
            Case {
                .name = "payload product complement",
                .source = "enum Value { Pair(bool, bool), } "
                          "fn select(value: Value) -> i32 => match value { "
                          ".Pair(true, _) => 1, .Pair(false, true) => 2, "
                          ".Pair(false, false) => 3, };",
                .rejection = {true, true, false},
            },
            Case {
                .name = "specialized arm bodies preserve selection coverage",
                .source =
                    "private fn select(const preferred: bool, value: bool) -> i32 "
                    "=> match value { true => const if preferred { 1 } else { 3 }, "
                    "false => 2, }; "
                    "fn root(value: bool) -> i32 => select(true, value) + select(false, value);",
                .rejection = {true, false},
            },
        };
        ct::each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto program = analyze_test_program(std::string(input.source));
            auto matches = 0uz;
            for (const auto entry : program.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.realized_region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* match = std::get_if<SemMatch>(&expression.value);
                        if (match == nullptr) {
                            return;
                        }
                        ++matches;
                        auto actual = std::vector<bool>();
                        auto arms = std::vector<PatternCoverageArm>();
                        for (const auto& arm : match->arms) {
                            actual.push_back(arm.pattern_may_reject);
                            arms.push_back(
                                {.alternatives = {arm.pattern}, .guarded = arm.guard.has_value()}
                            );
                        }
                        ct::expect(actual == input.rejection);
                        const auto coverage = compute_pattern_coverage(
                            program,
                            entry.value.pattern_table(),
                            match->subject->type.resolved(),
                            arms
                        );
                        if (!ct::expect(coverage.has_value())) {
                            return;
                        }
                        ct::expect(coverage->pattern_rejection == actual);
                    }
                );
            }
            ct::expect(matches > 0uz);
        });
    });
});

} // namespace
