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
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic control: try around an infallible body is silent") {
    const auto diagnostics = analyze_test_errors(
        "struct Failure {}\n"
        "fn valid() { try {} catch { Failure(_) => {}, } }\n"
    );
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic control: only executable fallthrough requires a return") {
    const auto terminal = analyze_test_errors(
        "private fn classify(value: bool) -> i32 {\n"
        "    if value { return 1; }\n"
        "    else if !value { return 2; }\n"
        "    else { return 3; }\n"
        "}\n"
    );
    CHECK_FALSE(contains_diagnostic_code(terminal, DiagnosticCode::FlowMissingReturn));

    const auto dead_fallthrough = analyze_test_errors(
        "private fn known() -> i32 {\n"
        "    if true { return 1; } else { let ignored = 0; }\n"
        "}\n"
    );
    CHECK_FALSE(contains_diagnostic_code(dead_fallthrough, DiagnosticCode::FlowMissingReturn));

    const auto live_fallthrough = analyze_test_errors(
        "private fn invalid(value: bool) -> i32 {\n"
        "    if value { return 1; }\n"
        "}\n"
    );
    CHECK(contains_diagnostic_code(live_fallthrough, DiagnosticCode::FlowMissingReturn));
}

TEST_CASE("Semantic structure: nested lambda owns a distinct body") {
    const auto program = analyze_test_program(
        "private fn outer() {\n"
        "    let callback = []() { let nested = 1; };\n"
        "}\n"
    );
    const auto callables = test_function_callables(program);
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
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn invalid() { let x = 1; if false { x = 2; } }"),
        DiagnosticCode::AccessImmutable
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn set(&x: i32) {} fn invalid() { let x = 1; if false { set(&x); } }"),
        DiagnosticCode::AccessImmutable
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn invalid() { var x = 1; if false { x = (&&x); } }"),
        DiagnosticCode::AccessOperationConflict
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn invalid() { let result = if true { 1 } else { false }; }"),
        DiagnosticCode::TypeMismatch
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors(
            "struct E {} fn fail() -> bool throw E { throw E {}; } "
            "fn invalid() { let value = false && fail(); }"
        ),
        DiagnosticCode::EffectUnmarked
    ));
    static_cast<void>(analyze_test_program(
        "fn valid() { var x = 1; if false { let moved = &&x; } let result = x; }"
    ));
}

TEST_CASE("Semantic control: irrefutable guard rejection retains completed writes") {
    static_cast<void>(analyze_test_program(
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
            analyze_test_errors(prefix + "fn run() -> i32 throw E { return " + expression + "; }");
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::NameUnresolved));
    }
    const auto missing_marker = analyze_test_errors(prefix + R"(
        fn fail() -> i32 throw E { throw E {}; }
        fn run() -> i32 throw E {
            return add(if true { throw E {}; } else { throw E {}; }, fail());
        }
    )");
    CHECK(contains_diagnostic_code(missing_marker, DiagnosticCode::EffectUnmarked));
    const auto unknown_callee = analyze_test_errors(prefix + R"(
        fn run() -> i32 throw E {
            return (if true { throw E {}; } else { throw E {}; })(1);
        }
    )");
    CHECK(contains_diagnostic_code(unknown_callee, DiagnosticCode::TypeNotCallable));
    const auto unknown_subject = analyze_test_errors(prefix + R"(
        fn run() -> i32 throw E {
            return match (if true { throw E {}; } else { throw E {}; }) { _ => 1 };
        }
    )");
    CHECK(contains_diagnostic_code(unknown_subject, DiagnosticCode::TypeValueRequired));
}

TEST_CASE("Functions: expression result dependencies are independent of module order") {
    for (const auto reverse : {false, true}) {
        auto sources = SourceManager();
        const auto consumer = sources.append_virtual(
            "consumer.cv",
            "import producer using next; fn first() => next(4); fn again() => first();"
        );
        const auto producer =
            sources.append_virtual("producer.cv", "export fn next(a: i32) => a + 1;");
        REQUIRE(consumer.has_value());
        REQUIRE(producer.has_value());
        auto inputs = std::array {
            CompilationModuleInput {
                .source_id = *consumer,
                .module_path = *CanonicalModulePath::from_value("consumer")
            },
            CompilationModuleInput {
                .source_id = *producer,
                .module_path = *CanonicalModulePath::from_value("producer")
            },
        };
        if (reverse) {
            std::ranges::reverse(inputs);
        }
        auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
        REQUIRE(parsed.has_value());
        auto result = analyze(std::move(*parsed));
        REQUIRE(result.has_value());
        const auto& program = result->value;
        CHECK_EQ(program.bodies().size(), 3uz);
        for (const auto callable : test_function_callables(program)) {
            const auto result_type = test_callable_signature(program, callable).result;
            const auto expected = CanonicalTypeValue {BuiltinTypeValue {BuiltinType::I32}};
            CHECK(program.types().type(result_type).value == expected);
        }
    }
}

TEST_CASE("Functions: inferred expression results obey visibility and return restrictions") {
    const auto visibility =
        analyze_test_errors("private struct Hidden {} export fn leak() => Hidden {};");
    CHECK(contains_diagnostic_code(visibility, DiagnosticCode::TypeVisibilityLeak));
    const auto boundary = analyze_test_errors("struct Value {} export(cpp) fn leak() => Value {};");
    CHECK(contains_diagnostic_code(boundary, DiagnosticCode::CppBoundaryType));
    const auto view = analyze_test_errors("fn leak(value: fn() -> void) => value;");
    CHECK(contains_diagnostic_code(view, DiagnosticCode::TypeCallableViewEscape));
    const auto captures =
        analyze_test_errors("fn leak() => []() { var local = 1; return [&local]() => local; }();");
    CHECK(contains_diagnostic_code(captures, DiagnosticCode::AccessBorrowConflict));
    const auto propagation = analyze_test_errors(
        "struct E {} private fn fail() -> i32 throw E { throw E {}; } private fn bad() => fail();"
    );
    CHECK(contains_diagnostic_code(propagation, DiagnosticCode::EffectUnmarked));
}

TEST_CASE("Functions: expression body returns retain expansion provenance") {
    const auto program = analyze_test_program("fn answer() => 42;");
    const auto callable = test_function_callables(program).front();
    const auto& body = program.bodies().body(*program.declarations().body_for_callable(callable));
    REQUIRE_EQ(body.region().statements.size(), 1uz);
    const auto& statement = body.region().statements.front();
    CHECK(std::holds_alternative<SemReturn>(statement.value));
    const auto& origin = program.provenance().origin(statement.origin);
    REQUIRE(std::holds_alternative<ProgramExpansionOrigin>(origin.value));
    CHECK_EQ(program.provenance().slice(statement.origin), "=>");
}
