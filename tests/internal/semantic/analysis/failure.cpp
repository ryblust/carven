module carven:test.internal.semantic.analysis.failure;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Semantic effects: constant-dead paths do not contribute outward failures",
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct DeadFailure {}\n"
                "private fn fail() -> bool throw DeadFailure { throw DeadFailure {}; }\n"
                "private fn valid() -> bool {\n"
                "    let short = false && fail()?;\n"
                "    if false { fail()?; }\n"
                "    while false { fail()?; }\n"
                "    return if true { short } else { fail()? };\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!ct::expect_equal(callables.size(), 2uz)) {
                return;
            }
            ct::expect(!(test_callable_failures(program, callables[0]).members.empty()));
            ct::expect(test_callable_failures(program, callables[1]).members.empty());

            const auto diagnostics = analyze_test_errors(
                "private fn invalid() {\n"
                "    if false { let value: bool = 1; }\n"
                "}\n"
            );
            ct::expect_diagnostic(diagnostics, DiagnosticCode::TypeMismatch);
        }
    );

    ct::test(
        "Semantic control: callable final signatures own their effective failures",
        [] static noexcept {
            const auto program = analyze_test_program(
                "private fn inferred() {}\n"
                "fn published() {}\n"
                "struct Failure {}\n"
                "fn declared() throw Failure {}\n"
            );
            const auto callables = test_function_callables(program);
            if (!ct::expect_equal(callables.size(), 3uz)) {
                return;
            }
            ct::expect(test_callable_failures(program, callables[0]).members.empty());
            ct::expect(test_callable_failures(program, callables[1]).members.empty());
            ct::expect_equal(test_callable_failures(program, callables[2]).members.size(), 1uz);
        }
    );

    ct::test(
        "Semantic effects: known match selection excludes only unexecuted failures",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Failure {}
        fn fail() -> i32 throw Failure { throw Failure {}; }
        private fn selected() -> i32 {
            let subject = 2;
            return match subject { 1 => fail()?, 2 => 7, _ => throw Failure {}, };
        }
        fn invoke() -> i32 => selected();
        private fn bounded() -> i32 {
            return match 2 { 3..fail()? => 1, _ => 7, };
        }
        fn declared() -> i32 throw Failure => match true { true => 7, false => 0, };
    )");
            const auto callables = test_function_callables(program);
            if (!ct::expect(callables.size() == 5uz)) {
                return;
            }
            ct::expect(test_callable_failures(program, callables[1]).members.empty());
            ct::expect(test_callable_failures(program, callables[2]).members.empty());
            ct::expect(test_callable_failures(program, callables[3]).members.size() == 1uz);
            ct::expect(test_callable_failures(program, callables[4]).members.size() == 1uz);
            ct::expect_diagnostic(
                analyze_test_errors("fn invalid() -> i32 => match true { true => 7, };"),
                DiagnosticCode::MatchNonExhaustive
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn invalid() -> i32 => match true { true => 7, false => false, };"
                ),
                DiagnosticCode::TypeMismatch
            );
        }
    );

    ct::test(
        "Semantic failures: implicit entry infers the escaping failure set",
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct First {} struct Second {}\n"
                "private fn later() throw First + Second { throw First {}; }\n"
                "later()?;\n"
                "throw Second {};\n"
            );
            const auto callables = test_function_callables(program);
            if (!ct::expect_equal(callables.size(), 2uz)) {
                return;
            }
            ct::expect_equal(test_callable_failures(program, callables.back()).members.size(), 2uz);

            const auto handled = analyze_test_program(
                "struct Failure {}\n"
                "private fn later() throw Failure { throw Failure {}; }\n"
                "try { later()?; } catch { Failure(_) => {}, }\n"
            );
            const auto handled_callables = test_function_callables(handled);
            if (!ct::expect_equal(handled_callables.size(), 2uz)) {
                return;
            }
            ct::expect(test_callable_failures(handled, handled_callables.back()).members.empty());
        }
    );

    ct::test(
        "Semantic failures: canonical set identity is independent of declaration order",
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
            if (!ct::expect_greater_equal(callables.size(), 2uz)) {
                return;
            }
            ct::expect(((test_callable_signature(program, callables[0]).failures)
                        == (test_callable_signature(program, callables[1]).failures)))
                .note(
                    "test_callable_signature(program, callables[0]).failures == test_callable_signature(program, callables[1]).failures"
                );

            ct::expect(test_callable_failures(program, callables.back()).members.empty());
        }
    );

    ct::test(
        "Semantic control: direct and mutually recursive inference reach one fixed point",
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
            if (!ct::expect_equal(callables.size(), 4uz)) {
                return;
            }
            for (auto index = 0uz; index < 3; ++index) {
                ct::expect_equal(
                    test_callable_failures(program, callables[index]).members.size(),
                    1uz
                );
            }
            ct::expect(((test_callable_signature(program, callables[0]).failures)
                        == (test_callable_signature(program, callables[1]).failures)))
                .note(
                    "test_callable_signature(program, callables[0]).failures == test_callable_signature(program, callables[1]).failures"
                );
            ct::expect(((test_callable_signature(program, callables[1]).failures)
                        == (test_callable_signature(program, callables[2]).failures)))
                .note(
                    "test_callable_signature(program, callables[1]).failures == test_callable_signature(program, callables[2]).failures"
                );
            ct::expect(test_callable_failures(program, callables[3]).members.empty());
        }
    );

    ct::test("Semantic control: fixed contracts are dependency boundaries", [] static noexcept {
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
        if (!ct::expect_equal(callables.size(), 3uz)) {
            return;
        }
        const auto fixed_failures = test_callable_failures(program, callables[1]).members;
        const auto caller_failures = test_callable_failures(program, callables[2]).members;
        if (!ct::expect_equal(fixed_failures.size(), 1uz)) {
            return;
        }
        ct::expect(((caller_failures) == (fixed_failures)))
            .note("caller_failures == fixed_failures");
    });

    ct::test(
        "Semantic failures: composite expressions retain every pending invocation",
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
            if (!ct::expect_equal(callables.size(), 5uz)) {
                return;
            }
            ct::expect_equal(test_callable_failures(program, callables[3]).members.size(), 2uz);
            ct::expect_equal(test_callable_failures(program, callables[4]).members.size(), 2uz);
        }
    );

    ct::test(
        "Semantic effects: known function calls use the target contract through view widening",
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
            if (!ct::expect(callables.size() == 4uz)) {
                return;
            }
            const auto& signature = program.callable_signatures().signature(
                program.declarations().callable(callables[1]).signature
            );
            ct::expect(program.failure_sets().failure_set(signature.failures).members.empty());
            const auto redundant = analyze_test_errors(R"(
        struct Failure {}
        fn plain() -> i32 => 7;
        fn invalid() -> i32 {
            let callback: fn() -> i32 throw Failure = plain;
            return callback()?;
        }
    )");
            ct::expect_diagnostic(redundant, DiagnosticCode::EffectPropagateRedundant);
        }
    );
});

} // namespace
