module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.control;

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

TEST_CASE("Semantic control: try around an infallible body is silent") {
    const auto diagnostics = analyze_errors(
        "struct Failure {}\n"
        "fn valid() { try {} catch { Failure(_) => {}, } }\n"
    );
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic control: only executable fallthrough requires a return") {
    const auto terminal = analyze_errors(
        "private fn classify(value: bool) -> i32 {\n"
        "    if value { return 1; }\n"
        "    else if !value { return 2; }\n"
        "    else { return 3; }\n"
        "}\n"
    );
    CHECK_FALSE(contains_code(terminal, DiagnosticCode::FlowMissingReturn));

    const auto dead_fallthrough = analyze_errors(
        "private fn known() -> i32 {\n"
        "    if true { return 1; } else { let ignored = 0; }\n"
        "}\n"
    );
    CHECK_FALSE(contains_code(dead_fallthrough, DiagnosticCode::FlowMissingReturn));

    const auto live_fallthrough = analyze_errors(
        "private fn invalid(value: bool) -> i32 {\n"
        "    if value { return 1; }\n"
        "}\n"
    );
    CHECK(contains_code(live_fallthrough, DiagnosticCode::FlowMissingReturn));
}

TEST_CASE("Semantic structure: nested lambda owns a distinct body") {
    const auto program = analyze_program(
        "private fn outer() {\n"
        "    let callback = []() { let nested = 1; };\n"
        "}\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 1uz);
    REQUIRE_EQ(std::ranges::distance(program.declarations().callables()), 2);
    REQUIRE_EQ(program.bodies().size(), 2);

    const auto outer_callable = callables.front();
    const auto outer_body = callable_body_id(program.declarations().callable(outer_callable));
    REQUIRE(outer_body.has_value());
    auto closure_callable = std::optional<CallableID>();
    for (const auto [id, declaration] : program.declarations().callables()) {
        if (std::holds_alternative<ClosureBodyImplementation>(declaration.implementation)) {
            closure_callable = id;
        }
    }
    REQUIRE(closure_callable.has_value());
    const auto nested_body = callable_body_id(program.declarations().callable(*closure_callable));
    REQUIRE(nested_body.has_value());
    CHECK_NE(*outer_body, *nested_body);
    CHECK_EQ(program.bodies().body(*outer_body).kind(), BodyKind::Function);
    CHECK_EQ(program.bodies().body(*nested_body).kind(), BodyKind::Closure);
}

TEST_CASE("Semantic source contracts: unreachable operations remain checked") {
    CHECK(contains_code(
        analyze_errors("fn invalid() { let x = 1; if false { x = 2; } }"),
        DiagnosticCode::AccessImmutable
    ));
    CHECK(contains_code(
        analyze_errors("fn set(&x: i32) {} fn invalid() { let x = 1; if false { set(&x); } }"),
        DiagnosticCode::AccessImmutable
    ));
    CHECK(contains_code(
        analyze_errors("fn invalid() { var x = 1; if false { x = (&&x); } }"),
        DiagnosticCode::AccessOperationConflict
    ));
    CHECK(contains_code(
        analyze_errors("fn invalid() { let result = if true { 1 } else { false }; }"),
        DiagnosticCode::TypeMismatch
    ));
    CHECK(contains_code(
        analyze_errors(
            "struct E {} fn fail() -> bool throw E { throw E {}; } "
            "fn invalid() { let value = false && fail(); }"
        ),
        DiagnosticCode::EffectUnmarked
    ));
    static_cast<void>(
        analyze_program("fn valid() { var x = 1; if false { let moved = &&x; } let result = x; }")
    );
}

TEST_CASE("Semantic control: irrefutable guard rejection retains completed writes") {
    static_cast<void>(analyze_program(
        "struct E {} fn fail() throw E { throw E {}; } "
        "fn valid() { var x = 1; let moved = &&x; "
        "match true { _ if if true { x = 2; false } else { false } => {}, _ => { let read = x; }, } "
        "let again = &&x; try { fail()?; } catch { "
        "E(_) if if true { x = 3; false } else { false } => {}, E(_) => { let read = x; }, } }"
    ));
}

TEST_CASE("Semantic control: every operand is checked after a nonreturning operand") {
    const auto prefix =
        std::string("struct E {}\nfn add(a: i32, b: i32) -> i32 { return a + b; }\n");
    for (const auto* expression : {
             "(if true { throw E {}; } else { throw E {}; }) + missing",
             "add(if true { throw E {}; } else { throw E {}; }, missing)",
         }) {
        const auto diagnostics =
            analyze_errors(prefix + "fn run() -> i32 throw E { return " + expression + "; }");
        CHECK(contains_code(diagnostics, DiagnosticCode::NameUnresolved));
    }
    const auto missing_marker = analyze_errors(prefix + R"(
        fn fail() -> i32 throw E { throw E {}; }
        fn run() -> i32 throw E {
            return add(if true { throw E {}; } else { throw E {}; }, fail());
        }
    )");
    CHECK(contains_code(missing_marker, DiagnosticCode::EffectUnmarked));
    const auto unknown_callee = analyze_errors(prefix + R"(
        fn run() -> i32 throw E {
            return (if true { throw E {}; } else { throw E {}; })(1);
        }
    )");
    CHECK(contains_code(unknown_callee, DiagnosticCode::TypeNotCallable));
    const auto unknown_subject = analyze_errors(prefix + R"(
        fn run() -> i32 throw E {
            return match (if true { throw E {}; } else { throw E {}; }) { _ => 1 };
        }
    )");
    CHECK(contains_code(unknown_subject, DiagnosticCode::TypeValueRequired));
}
