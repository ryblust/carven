module carven:test.internal.semantic.analysis.failure;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.diagnostics;
import :semantic.analysis.failure;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Semantic failures: guarded propagation reaches delayed inputs and "
    "filtered fixed points"_test = [] static noexcept {
        struct Scenario final {
            std::string_view name;
            bool source_first;
            bool seeded;
        };
        const auto scenarios = std::array {
            Scenario {.name = "source first", .source_first = true, .seeded = true},
            Scenario {.name = "gate first", .source_first = false, .seeded = true},
            Scenario {.name = "no surviving seeds", .source_first = false, .seeded = false},
        };
        const auto program = analyze_test_program("");
        each(
            scenarios,
            [](const Scenario& scenario) static noexcept { return scenario.name; },
            [&](const Scenario& scenario) noexcept {
                const auto provenance = CompilationProvenanceBuilder();
                const auto types = CanonicalTypeStoreBuilder(program.identity());
                const auto declarations =
                    DeclarationBuilder(program.identity(), provenance.identity());
                auto terms = FailureConstraintStore(program.identity(), provenance.identity());
                auto sets = FailureSetStoreBuilder(program.identity());
                const auto first = types.builtin_type(BuiltinType::I32);
                const auto second = types.builtin_type(BuiltinType::Bool);

                const auto target = terms.add_empty_term();
                const auto ready = terms.add_concrete_term(
                    scenario.seeded ? std::vector {first} : std::vector<TypeID> {}
                );
                const auto delayed = terms.add_concrete_term(
                    scenario.seeded ? std::vector {second} : std::vector<TypeID> {}
                );
                const auto first_hop = terms.add_union_term({delayed});
                const auto second_hop = terms.add_union_term({first_hop});
                terms.add_guarded_contribution(
                    target,
                    scenario.source_first ? second_hop : ready,
                    scenario.source_first ? ready : second_hop
                );
                terms.add_guarded_contribution(
                    target,
                    scenario.source_first ? second_hop : ready,
                    scenario.source_first ? ready : second_hop
                );
                terms.add_contribution(ready, ready);

                const auto cycle_left = terms.add_empty_term();
                const auto cycle_right = terms.add_empty_term();
                terms.equate(cycle_left, cycle_right);
                terms.add_guarded_contribution(cycle_left, cycle_right, cycle_left);
                const auto disabled = terms.add_empty_term();
                terms.add_guarded_contribution(disabled, cycle_left, ready);

                const auto excluded = terms.add_residual_term(ready, {first});
                terms.add_member(excluded, first);
                const auto retained = terms.add_intersection_term(ready, {second});
                terms.add_member(retained, first);
                const auto filtered = terms.add_union_term({excluded, retained});
                const auto residual_growth = terms.add_residual_term(ready, {first});
                terms.add_member(residual_growth, first);
                terms.add_contribution(residual_growth, second_hop);
                const auto retained_growth = terms.add_intersection_term(ready, {second});
                terms.add_member(retained_growth, first);
                terms.add_contribution(retained_growth, second_hop);

                auto diagnostics = DiagnosticSink();
                const auto solved = solve_failure_constraints(
                    std::move(terms).finish(),
                    sets,
                    provenance.reader(),
                    FailureTypeDiagnosticNames(
                        types,
                        declarations.construction_view(),
                        provenance.reader()
                    ),
                    AnalysisDiagnostics(diagnostics)
                );
                if (!expect_equal(solved.has_value(), true)) {
                    return;
                }
                expect_equal(diagnostics.empty(), true);
                const auto expected_target = scenario.seeded
                    ? std::vector {scenario.source_first ? first : second}
                    : std::vector<TypeID> {};
                expect(sets.copy(solved->failure_set(target)).members == expected_target)
                    .note("guarded contribution retains its source members");
                const auto expected_growth =
                    scenario.seeded ? std::vector {second} : std::vector<TypeID> {};
                for (const auto term : {first_hop, second_hop, residual_growth, retained_growth}) {
                    expect(sets.copy(solved->failure_set(term)).members == expected_growth)
                        .note("delayed propagation and filtering retain the second member");
                }
                const auto empty_set = sets.empty_set();
                for (const auto term :
                     {cycle_left, cycle_right, disabled, excluded, retained, filtered}) {
                    expect_equal(solved->contains(term), true);
                    expect(solved->failure_set(term) == empty_set)
                        .note("empty terms use the canonical empty failure set");
                    expect_equal(sets.copy(solved->failure_set(term)).members.empty(), true);
                }
            }
        );
    };

    "Semantic effects: every branch contributes its failures whatever its condition"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Failure {}
            private fn fail() -> bool throw Failure { throw Failure {}; }
            private fn runtime_if() { let flag = false; if flag { fail()?; } }
            private fn runtime_logic() -> bool { let flag = false; return flag && fail()?; }
            private fn runtime_match() -> bool {
                let subject = true; return match subject { true => true, false => fail()?, };
            }
            private fn named_if() { const flag = false; if flag { fail()?; } }
            private fn named_logic() -> bool { const flag = false; return flag && fail()?; }
            private fn named_match() -> bool {
                const subject = true; return match subject { true => true, false => fail()?, };
            }
            private fn literal_if() { if false { fail()?; } }
            private fn literal_logic() -> bool => false && fail()?;
            private fn literal_while() { while false { fail()?; } }
            private fn literal_value() -> bool => if true { true } else { fail()? };
            private fn literal_match() -> bool => match true { true => true, false => fail()?, };
            private fn static_if() { const if false { fail()?; } }
        )");
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 13uz)) {
                return;
            }
            for (auto index = 0uz; index < callables.size(); ++index) {
                scenario(std::format("function {}", index), [&]() noexcept {
                    expect_equal(
                        test_callable_failures(program, callables[index]).members.size(),
                        1uz
                    );
                });
            }
            expect_diagnostic(
                analyze_test_errors("private fn invalid() { if false { let value: bool = 1; } }"),
                DiagnosticCode::TypeMismatch
            );
        };

    "Semantic control: callable final signatures own their effective failures"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "private fn inferred() {}\n"
                "fn published() {}\n"
                "struct Failure {}\n"
                "fn declared() throw Failure {}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 3uz)) {
                return;
            }
            expect(test_callable_failures(program, callables[0]).members.empty());
            expect(test_callable_failures(program, callables[1]).members.empty());
            expect_equal(test_callable_failures(program, callables[2]).members.size(), 1uz);
        };

    "Semantic effects: match bounds and coverage follow patterns, not the subject value"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Failure {}
        fn fail() -> i32 throw Failure { throw Failure {}; }
        private fn bounded() -> i32 {
            return match 2 { 3..fail()? => 1, _ => 7, };
        }
        fn declared() -> i32 throw Failure => match true { true => 7, false => 0, };
    )");
            const auto callables = test_function_callables(program);
            if (!expect(callables.size() == 3uz)) {
                return;
            }
            expect(test_callable_failures(program, callables[1]).members.size() == 1uz);
            expect(test_callable_failures(program, callables[2]).members.size() == 1uz);
            expect_diagnostic(
                analyze_test_errors("fn invalid() -> i32 => match true { true => 7, };"),
                DiagnosticCode::MatchNonExhaustive
            );
            expect_diagnostic(
                analyze_test_errors(
                    "fn invalid() -> i32 => match true { true => 7, false => false, };"
                ),
                DiagnosticCode::TypeMismatch
            );
        };

    "Semantic failures: implicit entry infers the escaping failure set"_test = [] static noexcept {
        const auto program = analyze_test_program(
            "struct First {} struct Second {}\n"
            "private fn later() throw First + Second { throw First {}; }\n"
            "later()?;\n"
            "throw Second {};\n"
        );
        const auto callables = test_function_callables(program);
        if (!expect_equal(callables.size(), 2uz)) {
            return;
        }
        expect_equal(test_callable_failures(program, callables.back()).members.size(), 2uz);

        const auto handled = analyze_test_program(
            "struct Failure {}\n"
            "private fn later() throw Failure { throw Failure {}; }\n"
            "try { later()?; } catch { Failure(_) => {}, }\n"
        );
        const auto handled_callables = test_function_callables(handled);
        if (!expect_equal(handled_callables.size(), 2uz)) {
            return;
        }
        expect(test_callable_failures(handled, handled_callables.back()).members.empty());
    };

    "Semantic failures: canonical set identity is independent of declaration order"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct AlphaFailure {}\n"
                "struct BetaFailure {}\n"
                "private fn alpha_first() throw AlphaFailure + BetaFailure {}\n"
                "private fn beta_first() throw BetaFailure + AlphaFailure {}\n"
                "private fn handled() {\n"
                "    try { alpha_first()?; } catch { AlphaFailure(_) => {}, BetaFailure(_) => {}, }\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_greater_equal(callables.size(), 2uz)) {
                return;
            }
            expect(((test_callable_signature(program, callables[0]).failures)
                    == (test_callable_signature(program, callables[1]).failures)))
                .note(
                    "test_callable_signature(program, callables[0]).failures == test_callable_signature(program, callables[1]).failures"
                );

            expect(test_callable_failures(program, callables.back()).members.empty());
        };

    "Semantic control: direct and mutually recursive inference reach one fixed point"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct Failure {}\n"
                "private fn direct() { throw Failure {}; }\n"
                "private fn left(stop: bool) -> void {\n"
                "    if stop { throw Failure {}; }\n"
                "    right(true)?;\n"
                "}\n"
                "private fn right(stop: bool) -> void {\n"
                "    if stop { left(true)?; }\n"
                "}\n"
                "fn recover() {\n"
                "    try { direct()?; left(false)?; } catch { Failure(_) => {}, }\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 4uz)) {
                return;
            }
            for (auto index = 0uz; index < 3; ++index) {
                expect_equal(test_callable_failures(program, callables[index]).members.size(), 1uz);
            }
            expect(((test_callable_signature(program, callables[0]).failures)
                    == (test_callable_signature(program, callables[1]).failures)))
                .note(
                    "test_callable_signature(program, callables[0]).failures == test_callable_signature(program, callables[1]).failures"
                );
            expect(((test_callable_signature(program, callables[1]).failures)
                    == (test_callable_signature(program, callables[2]).failures)))
                .note(
                    "test_callable_signature(program, callables[1]).failures == test_callable_signature(program, callables[2]).failures"
                );
            expect(test_callable_failures(program, callables[3]).members.empty());
        };

    "Semantic control: fixed contracts are dependency boundaries"_test = [] static noexcept {
        const auto program = analyze_test_program(
            "struct DeclaredFailure {}\n"
            "struct BodyFailure {}\n"
            "private fn source() { throw BodyFailure {}; }\n"
            "private fn fixed(fail: bool) throw DeclaredFailure {\n"
            "    if fail { throw DeclaredFailure {}; }\n"
            "    try { source()?; } catch { BodyFailure(_) => {}, }\n"
            "}\n"
            "private fn caller() { fixed(true)?; }\n"
        );
        const auto callables = test_function_callables(program);
        if (!expect_equal(callables.size(), 3uz)) {
            return;
        }
        const auto fixed_failures = test_callable_failures(program, callables[1]).members;
        const auto caller_failures = test_callable_failures(program, callables[2]).members;
        if (!expect_equal(fixed_failures.size(), 1uz)) {
            return;
        }
        expect(((caller_failures) == (fixed_failures))).note("caller_failures == fixed_failures");
    };

    "Semantic failures: composite expressions retain every pending invocation"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct FirstFailure {}\n"
                "struct SecondFailure {}\n"
                "struct Pair { left: i32, right: i32 }\n"
                "private fn first() -> i32 throw FirstFailure { throw FirstFailure {}; }\n"
                "private fn second() -> i32 throw SecondFailure { throw SecondFailure {}; }\n"
                "private fn sum(pair: Pair) -> i32 { return pair.left + pair.right; }\n"
                "private fn binary() -> i32 throw FirstFailure + SecondFailure {\n"
                "    return (first() + second())?;\n"
                "}\n"
                "private fn aggregate() -> i32 throw FirstFailure + SecondFailure {\n"
                "    return sum({ left: first(), right: second() })?;\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 5uz)) {
                return;
            }
            expect_equal(test_callable_failures(program, callables[3]).members.size(), 2uz);
            expect_equal(test_callable_failures(program, callables[4]).members.size(), 2uz);
        };

    "Semantic effects: known function calls use the target contract through view widening"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Failure {}
        fn plain() -> i32 => 7;
        fn known() -> i32 {
            let callback: fn() -> i32 throw Failure = plain;
            let copy = callback;
            return copy();
        }
        fn declared() -> i32 throw Failure => 7;
        fn known_declared() -> i32 throw Failure {
            let callback: fn() -> i32 throw Failure = declared;
            return callback()?;
        }
    )");
            const auto callables = test_function_callables(program);
            if (!expect(callables.size() == 4uz)) {
                return;
            }
            const auto& signature = program.callable_signatures().signature(
                program.declarations().callable(callables[1]).signature
            );
            expect(program.failure_sets().failure_set(signature.failures).members.empty());
            const auto redundant = analyze_test_errors(R"(
        struct Failure {}
        fn plain() -> i32 => 7;
        fn invalid() -> i32 {
            let callback: fn() -> i32 throw Failure = plain;
            return callback()?;
        }
    )");
            expect_diagnostic(redundant, DiagnosticCode::EffectPropagateRedundant);
        };
});

} // namespace
