module carven:test.internal.semantic.analysis.match_selection;

import :diagnostics.code;
import :semantic.analysis.coverage;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.harness.diagnostics;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Match selection: rejection facts follow the remaining domain"_test = [] static noexcept {
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
                .name = "tuple product complement",
                .source = "fn select(value: (bool, bool)) -> i32 => match value { "
                          "(true, _) => 1, (false, true) => 2, (false, false) => 3, };",
                .rejection = {true, true, false},
            },
            Case {
                .name = "guarded tuple complement",
                .source = "fn select(value: (bool, bool), accept: bool) -> i32 => match value { "
                          "(true, _) if accept => 1, (_, true) => 2, (_, false) => 3, };",
                .rejection = {true, true, false},
            },
            Case {
                .name = "nested tuple and enum product",
                .source = "enum Choice { Left(bool), Right, } "
                          "fn select(value: (Choice, (bool, void))) -> i32 => match value { "
                          "(.Left(true), (_, ())) => 1, (.Left(false), (_, ())) => 2, "
                          "(.Right, (true, ())) => 3, (.Right, (false, ())) => 4, };",
                .rejection = {true, true, true, false},
            },
            Case {
                .name = "void product",
                .source = "fn select(value: void) -> i32 => match value { () => 1, };",
                .rejection = {false},
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
        each(cases, &Case::name, [](const Case& input) static noexcept {
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
                        expect(actual == input.rejection);
                        const auto coverage = compute_pattern_coverage(
                            program,
                            entry.value.pattern_table(),
                            match->subject->type.resolved(),
                            arms
                        );
                        if (!expect(coverage.has_value())) {
                            return;
                        }
                        expect(coverage->pattern_rejection == actual);
                    }
                );
            }
            expect(matches > 0uz);
        });
    };

    "Match selection: tuple coverage preserves whole product correlations"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(
                    "fn select(value: (bool, bool)) -> i32 => match value { "
                    "(true, _) | (_, true) => 1, };"
                ),
                DiagnosticCode::MatchNonExhaustive
            );
            expect_diagnostic(
                analyze_test_errors(
                    "fn select(value: (bool, bool)) -> i32 => match value { "
                    "(true, _) | (true, false) => 1, _ => 2, };"
                ),
                DiagnosticCode::MatchDuplicateAlternative
            );
            expect_diagnostic(
                analyze_test_errors(
                    "fn select(value: (bool, bool)) -> i32 => match value { "
                    "(true, _) => 1, (true, false) => 2, (false, _) => 3, };"
                ),
                DiagnosticCode::FlowUnreachableMatchArm
            );
            expect_diagnostic(
                analyze_test_errors(
                    "fn select(value: (bool, bool), accept: bool) -> i32 => match value { "
                    "(true, _) if accept => 1, (false, _) => 2, };"
                ),
                DiagnosticCode::MatchNonExhaustive
            );
        };

    "Match selection: tuple patterns validate shape and binding identity"_test =
        [] static noexcept {
            const auto invalid_shapes = std::array {
                "fn select(value: (bool, bool)) -> i32 => match value { (item,) => 1, _ => 2, };",
                "fn select(value: i32) -> i32 => match value { () => 1, _ => 2, };",
                "fn select(value: (i32, bool)) -> i32 => match value { (_, 1) => 1, _ => 2, };",
            };
            for (const auto source : invalid_shapes) {
                expect_diagnostic(analyze_test_errors(source), DiagnosticCode::TypeMatchPattern);
            }
            expect_diagnostic(
                analyze_test_errors(
                    "fn select(value: (bool, bool)) -> i32 => match value { "
                    "(item, item) => 1, };"
                ),
                DiagnosticCode::NameDuplicateLocal
            );
            expect_diagnostic(
                analyze_test_errors(
                    "fn select(value: (i32, bool)) -> i32 => match value { "
                    "(item, _) | (_, item) => 1, };"
                ),
                DiagnosticCode::MatchBindingMismatch
            );
        };
});

} // namespace
