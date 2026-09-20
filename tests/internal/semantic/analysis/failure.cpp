module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

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
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic effects: constant-dead paths do not contribute outward failures") {
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
    REQUIRE_EQ(callables.size(), 2uz);
    CHECK_FALSE(test_callable_failures(program, callables[0]).members.empty());
    CHECK(test_callable_failures(program, callables[1]).members.empty());

    const auto diagnostics = analyze_test_errors(
        "private fn invalid() {\n"
        "    if false { let value: bool = 1; }\n"
        "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::TypeMismatch));
}

TEST_CASE("Semantic control: callable final signatures own their effective failures") {
    const auto program = analyze_test_program(
        "private fn inferred() {}\n"
        "fn published() {}\n"
        "struct Failure {}\n"
        "fn declared() throw Failure {}\n"
    );
    const auto callables = test_function_callables(program);
    REQUIRE_EQ(callables.size(), 3uz);
    CHECK(test_callable_failures(program, callables[0]).members.empty());
    CHECK(test_callable_failures(program, callables[1]).members.empty());
    CHECK_EQ(test_callable_failures(program, callables[2]).members.size(), 1uz);
}

TEST_CASE("Semantic effects: known match selection excludes only unexecuted failures") {
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
    REQUIRE(callables.size() == 5uz);
    CHECK(test_callable_failures(program, callables[1]).members.empty());
    CHECK(test_callable_failures(program, callables[2]).members.empty());
    CHECK(test_callable_failures(program, callables[3]).members.size() == 1uz);
    CHECK(test_callable_failures(program, callables[4]).members.size() == 1uz);
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn invalid() -> i32 => match true { true => 7, };"),
        DiagnosticCode::MatchNonExhaustive
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn invalid() -> i32 => match true { true => 7, false => false, };"),
        DiagnosticCode::TypeMismatch
    ));
}

TEST_CASE("Semantic failures: implicit entry infers the escaping failure set") {
    const auto program = analyze_test_program(
        "struct First {} struct Second {}\n"
        "private fn later() throw First + Second { throw First {}; }\n"
        "later()?;\n"
        "throw Second {};\n"
    );
    const auto callables = test_function_callables(program);
    REQUIRE_EQ(callables.size(), 2uz);
    CHECK_EQ(test_callable_failures(program, callables.back()).members.size(), 2uz);

    const auto handled = analyze_test_program(
        "struct Failure {}\n"
        "private fn later() throw Failure { throw Failure {}; }\n"
        "try { later()?; } catch { Failure(_) => {}, }\n"
    );
    const auto handled_callables = test_function_callables(handled);
    REQUIRE_EQ(handled_callables.size(), 2uz);
    CHECK(test_callable_failures(handled, handled_callables.back()).members.empty());
}

TEST_CASE("Semantic failures: canonical set identity is independent of declaration order") {
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
    REQUIRE_GE(callables.size(), 2uz);
    CHECK_EQ(
        test_callable_signature(program, callables[0]).failures,
        test_callable_signature(program, callables[1]).failures
    );

    CHECK(test_callable_failures(program, callables.back()).members.empty());
}

TEST_CASE("Semantic control: direct and mutually recursive inference reach one fixed point") {
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
    REQUIRE_EQ(callables.size(), 4uz);
    for (auto index = 0uz; index < 3; ++index) {
        CHECK_EQ(test_callable_failures(program, callables[index]).members.size(), 1uz);
    }
    CHECK_EQ(
        test_callable_signature(program, callables[0]).failures,
        test_callable_signature(program, callables[1]).failures
    );
    CHECK_EQ(
        test_callable_signature(program, callables[1]).failures,
        test_callable_signature(program, callables[2]).failures
    );
    CHECK(test_callable_failures(program, callables[3]).members.empty());
}

TEST_CASE("Semantic control: fixed contracts are dependency boundaries") {
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
    REQUIRE_EQ(callables.size(), 3uz);
    const auto fixed_failures = test_callable_failures(program, callables[1]).members;
    const auto caller_failures = test_callable_failures(program, callables[2]).members;
    REQUIRE_EQ(fixed_failures.size(), 1);
    CHECK_EQ(caller_failures, fixed_failures);
}

TEST_CASE("Semantic failures: composite expressions retain every pending invocation") {
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
        "    return sum(Pair { left: first(), right: second() })?;\n"
        "}\n"
    );
    const auto callables = test_function_callables(program);
    REQUIRE_EQ(callables.size(), 5uz);
    CHECK_EQ(test_callable_failures(program, callables[3]).members.size(), 2uz);
    CHECK_EQ(test_callable_failures(program, callables[4]).members.size(), 2uz);
}

TEST_CASE("Semantic effects: known function calls use the target contract through view widening") {
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
    REQUIRE(callables.size() == 4uz);
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(callables[1]).signature
    );
    CHECK(program.failure_sets().failure_set(signature.failures).members.empty());
    const auto redundant = analyze_test_errors(R"(
        struct Failure {}
        fn plain() -> i32 => 7;
        fn invalid() -> i32 {
            let callback: fn() -> i32 throw Failure = plain;
            return callback()?;
        }
    )");
    CHECK(contains_diagnostic_code(redundant, DiagnosticCode::EffectPropagateRedundant));
}
