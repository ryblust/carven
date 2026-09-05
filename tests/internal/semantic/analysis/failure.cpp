module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.failure;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :test.internal.semantic.analysis.fixture;
import std;

using semantic_analysis_test::analyze_errors;
using semantic_analysis_test::analyze_program;
using semantic_analysis_test::contains_code;
using semantic_analysis_test::function_callables;
using semantic_analysis_test::signature;
using semantic_analysis_test::failures;

TEST_CASE("Semantic effects: constant-dead paths do not contribute outward failures") {
    const auto program = analyze_program(
        "struct DeadFailure {}\n"
        "private fn fail() -> bool throw DeadFailure { throw DeadFailure {}; }\n"
        "private fn valid() -> bool {\n"
        "    let short = false && fail()?;\n"
        "    if false { fail()?; }\n"
        "    while false { fail()?; }\n"
        "    return if true { short } else { fail()? };\n"
        "}\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 2uz);
    CHECK_FALSE(failures(program, callables[0]).members.empty());
    CHECK(failures(program, callables[1]).members.empty());

    const auto diagnostics = analyze_errors(
        "private fn invalid() {\n"
        "    if false { let value: bool = 1; }\n"
        "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::TypeMismatch));
}

TEST_CASE("Semantic control: callable final signatures own their effective failures") {
    const auto program = analyze_program(
        "private fn inferred() {}\n"
        "fn published() {}\n"
        "struct Failure {}\n"
        "fn declared() throw Failure {}\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 3uz);
    CHECK(failures(program, callables[0]).members.empty());
    CHECK(failures(program, callables[1]).members.empty());
    CHECK_EQ(failures(program, callables[2]).members.size(), 1uz);
}

TEST_CASE("Semantic failures: canonical set identity is independent of declaration order") {
    const auto program = analyze_program(
        "struct AlphaFailure {}\n"
        "struct BetaFailure {}\n"
        "private fn alpha_first() throw AlphaFailure + BetaFailure {}\n"
        "private fn beta_first() throw BetaFailure + AlphaFailure {}\n"
        "private fn handled() {\n"
        "    try { alpha_first()?; } catch { AlphaFailure(_) => {}, BetaFailure(_) => {}, }\n"
        "}\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_GE(callables.size(), 2uz);
    CHECK_EQ(signature(program, callables[0]).failures, signature(program, callables[1]).failures);

    CHECK(failures(program, callables.back()).members.empty());
}

TEST_CASE("Semantic control: direct and mutually recursive inference reach one fixed point") {
    const auto program = analyze_program(
        "struct Failure {}\n"
        "private fn direct() { throw Failure {}; }\n"
        "private fn left(stop: bool) {\n"
        "    if stop { throw Failure {}; }\n"
        "    right(true)?;\n"
        "}\n"
        "private fn right(stop: bool) {\n"
        "    if stop { left(true)?; }\n"
        "}\n"
        "fn recover() {\n"
        "    try { direct()?; left(false)?; } catch { Failure(_) => {}, }\n"
        "}\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 4uz);
    for (auto index = 0uz; index < 3; ++index) {
        CHECK_EQ(failures(program, callables[index]).members.size(), 1uz);
    }
    CHECK_EQ(signature(program, callables[0]).failures, signature(program, callables[1]).failures);
    CHECK_EQ(signature(program, callables[1]).failures, signature(program, callables[2]).failures);
    CHECK(failures(program, callables[3]).members.empty());
}

TEST_CASE("Semantic control: fixed contracts are dependency boundaries") {
    const auto program = analyze_program(
        "struct DeclaredFailure {}\n"
        "struct BodyFailure {}\n"
        "private fn source() { throw BodyFailure {}; }\n"
        "private fn fixed(fail: bool) throw DeclaredFailure {\n"
        "    if fail { throw DeclaredFailure {}; }\n"
        "    try { source()?; } catch { BodyFailure(_) => {}, }\n"
        "}\n"
        "private fn caller() { fixed(true)?; }\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 3uz);
    const auto fixed_failures = failures(program, callables[1]).members;
    const auto caller_failures = failures(program, callables[2]).members;
    REQUIRE_EQ(fixed_failures.size(), 1);
    CHECK_EQ(caller_failures, fixed_failures);
}

TEST_CASE("Semantic failures: composite expressions retain every pending invocation") {
    const auto program = analyze_program(
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
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 5uz);
    CHECK_EQ(failures(program, callables[3]).members.size(), 2uz);
    CHECK_EQ(failures(program, callables[4]).members.size(), 2uz);
}
