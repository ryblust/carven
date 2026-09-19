module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.control;

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
import :source.batch;
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
    static_cast<void>(analyze_test_program(
        "private fn classify(value: bool) -> i32 {\n"
        "    if value { return 1; }\n"
        "    else if !value { return 2; }\n"
        "    else { return 3; }\n"
        "}\n"
    ));

    static_cast<void>(analyze_test_program(
        "private fn known() -> i32 {\n"
        "    if true { return 1; } else { let ignored = 0; }\n"
        "}\n"
    ));

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
            SourceModuleInput {
                .source_id = *consumer,
                .module_path = *CanonicalModulePath::from_value("consumer")
            },
            SourceModuleInput {
                .source_id = *producer,
                .module_path = *CanonicalModulePath::from_value("producer")
            },
        };
        if (reverse) {
            std::ranges::reverse(inputs);
        }
        auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
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

TEST_CASE("Semantic control: test stop propagates through callable dependencies") {
    const auto program = analyze_test_program(R"(
        fn forward() { middle(); middle(); }
        fn middle() { stop(); }
        fn stop() { fail(); }
        fn cycle(flag: bool) -> void { if flag { back(false); } else { stop(); } }
        fn back(flag: bool) -> void { cycle(flag); }
        fn quiet(flag: bool) -> void { if flag { quiet_back(false); } }
        fn quiet_back(flag: bool) -> void { quiet(flag); }
        fn ordinary() { check(true); }
        private import(cpp) fn native();
        fn call_view(action: fn() -> void) { action(); }
        fn closure() { []() { fail(); }(); }
    )");
    const auto expected =
        std::array {true, true, true, true, true, false, false, false, false, true, true};
    const auto callables = test_function_callables(program);
    REQUIRE(callables.size() == expected.size());
    for (auto index = 0uz; index < expected.size(); ++index) {
        CAPTURE(index);
        CHECK(program.may_stop_test(callables[index]) == expected[index]);
    }
}

TEST_CASE("Semantic control: published expressions include conservative call effects") {
    const auto program = analyze_test_program(R"(
        fn direct() -> i32 { return if false { require(false); 1 } else { 2 }; }
        fn indirect() -> i32 { return if false { stop(); 1 } else { 2 }; }
        fn stop() { fail(); }
        test "nested calls" { let observed = if true { stop(); 1 } else { 2 }; check(observed == 1); }
    )");
    const auto callables = test_function_callables(program);
    REQUIRE(callables.size() == 3uz);
    for (auto index = 0uz; index < 2uz; ++index) {
        const auto& body =
            program.bodies().body(*program.declarations().body_for_callable(callables[index]));
        const auto* returned = std::get_if<SemReturn>(&body.region().statements.front().value);
        REQUIRE(returned != nullptr);
        REQUIRE(returned->value.has_value());
        CHECK(returned->value->exits_test == (index == 1uz));
    }
    for (const auto entry : program.tests().entries()) {
        const auto& body = program.bodies().body(entry.value.body);
        const auto* initialized =
            std::get_if<SemInitialize>(&body.region().statements.front().value);
        REQUIRE(initialized != nullptr);
        CHECK(initialized->initializer.exits_test);
    }
}

TEST_CASE("Semantic control: native invocations inherit argument completion") {
    const auto invocations = std::array {
        "::native_call(if {} {{ fail(\"stop\"); }} else {{ 1 }})",
        "::Native {{ if {} {{ fail(\"stop\"); }} else {{ 1 }} }}",
    };
    for (const auto invocation : invocations) {
        CAPTURE(invocation);
        const auto body = [&](std::string_view condition) noexcept {
            return std::format(
                "private fn run() -> i32 {{ {}; }}",
                std::vformat(invocation, std::make_format_args(condition))
            );
        };
        static_cast<void>(analyze_test_program(body("true")));
        CHECK(contains_diagnostic_code(
            analyze_test_errors(body("false")),
            DiagnosticCode::FlowMissingReturn
        ));
    }
}

TEST_CASE("Semantic ranges: coverage partitions integer domains and nested payloads") {
    static_cast<void>(analyze_test_program(
        "enum E { Value(u8), Empty }\n"
        "fn classify(x: E) -> i32 { return match x { .Value(0..128) => 0, "
        ".Value(128..=255) => 1, .Empty => 2 }; }\n"
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors(
            "fn f(x: u8) -> i32 { return match x { 0..127 => 0, 128..=255 => 1 }; }"
        ),
        DiagnosticCode::MatchNonExhaustive
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn f(x: i32, lo: i32) -> i32 { return match x { lo.. => 0 }; }"),
        DiagnosticCode::MatchNonExhaustive
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors(
            "fn f(x: i32) -> i32 { return match x { 0..10 => 0, 10..20 => 1, 1..19 => 2, _ => 3 }; }"
        ),
        DiagnosticCode::FlowUnreachableMatchArm
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn f(x: i32) -> i32 { return match x { 0..10 | 4 => 0, _ => 1 }; }"),
        DiagnosticCode::MatchDuplicateAlternative
    ));
    CHECK(contains_diagnostic_code(
        analyze_test_errors("fn f(x: f64) -> i32 { return match x { 0..10 => 0, _ => 1 }; }"),
        DiagnosticCode::TypeMatchPattern
    ));
}

TEST_CASE("Semantic ranges: selection protects the subject and excludes dead bound failures") {
    const auto subject_write = analyze_test_errors(
        "fn change(&x: i32) -> i32 { x = 0; return 0; } "
        "fn f() { var x = 1; match x { (change(&x))..10 => {}, _ => {} } }"
    );
    CHECK(contains_diagnostic_code(subject_write, DiagnosticCode::AccessOperationConflict));
    const auto pattern_binding = analyze_test_errors(
        "enum E { Pair(i32, i32) } "
        "fn f(x: E) { match x { .Pair(lo, lo..10) => {}, _ => {} } }"
    );
    CHECK(contains_diagnostic_code(pattern_binding, DiagnosticCode::TypeMatchPattern));
    static_cast<void>(analyze_test_program(
        "struct E {} fn bound() -> i32 throw E { throw E {}; } "
        "fn f(x: i32) -> i32 { return match x { _ => 1, (bound()?)..10 => 2 }; }"
    ));
}

TEST_CASE("Semantic ranges: bounds and element types obey integer contracts") {
    struct Case final {
        std::string_view name;
        std::string_view source;
        DiagnosticCode code;
    };

    const auto cases = std::array {
        Case {
            "write integer element",
            "fn f() { for &n in 0..10 {} }",
            DiagnosticCode::AccessRangeBinding
        },
        Case {"noninteger element", "fn f(x: range<f64>) {}", DiagnosticCode::TypeRangeInteger},
        Case {"missing element", "fn f(x: range) {}", DiagnosticCode::TypeUnresolved},
        Case {"extra element", "fn f(x: range<i32, u8>) {}", DiagnosticCode::TypeUnresolved},
        Case {
            "noninteger bounds",
            "fn f() { let r = 0.0..1.0; }",
            DiagnosticCode::TypeRangeInteger
        },
        Case {
            "different value bounds",
            "fn f(a: i32, b: u8) { let r = a..b; }",
            DiagnosticCode::TypeRangeBounds
        },
        Case {
            .name = "suffixed lower bound keeps its type",
            .source = "fn f(end: usize) { let r = 0i32..end; }",
            .code = DiagnosticCode::TypeRangeBounds,
        },
        Case {
            .name = "inferred binding keeps its type",
            .source = "fn f(end: usize) { let begin = 0; let r = begin..end; }",
            .code = DiagnosticCode::TypeRangeBounds,
        },
        Case {
            .name = "grouped lower bound does not select sibling context",
            .source = "fn f(end: usize) { let r = (0)..end; }",
            .code = DiagnosticCode::TypeRangeBounds,
        },
        Case {
            .name = "negative lower bound does not select sibling context",
            .source = "fn f(end: usize) { let r = -1..end; }",
            .code = DiagnosticCode::TypeRangeBounds,
        },
        Case {
            .name = "contextual lower bound must fit",
            .source = "fn f(end: u8) { let r = 256..end; }",
            .code = DiagnosticCode::ConstLiteralRange,
        },
        Case {
            .name = "required contextual lower bound must fit",
            .source = "const end = 3u8; const r = 256..=end;",
            .code = DiagnosticCode::ConstLiteralRange,
        },
        Case {
            .name = "floating sibling is not converted to an integer bound",
            .source = "fn f(end: f64) { let r = 0..end; }",
            .code = DiagnosticCode::TypeRangeBounds,
        },
        Case {
            "different pattern bound",
            "fn f(x: i32, b: u8) { match x { b.. => {}, _ => {} } }",
            DiagnosticCode::TypeRangeBounds
        }
    };
    for (const auto& scenario : cases) {
        CAPTURE(scenario.name);
        CHECK(contains_diagnostic_code(
            analyze_test_errors(std::string(scenario.source)),
            scenario.code
        ));
    }
    static_cast<void>(analyze_test_program("fn f() { for n: u8 in (0..=255) {} }"));
}

TEST_CASE("Semantic ranges: bound sequencing preserves established pointer facts") {
    static_cast<void>(analyze_test_program(R"(
        fn f(p: ptr<i32>, x: i32) -> i32 {
            if p == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return match x {
                (if true { slot = p; 0 } else { 0 })..0 | 0..(*slot) => *slot,
                _ => *slot
            };
        }
    )"));
    static_cast<void>(analyze_test_program(R"(
        enum E { Number(i32) }
        fn f(p: ptr<i32>, x: E) -> i32 {
            if p == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return match x {
                .Number((if true { slot = p; 0 } else { 0 })..(*slot)) => *slot,
                _ => *slot
            };
        }
    )"));

    static_cast<void>(analyze_test_program(R"(
        fn f(p: ptr<i32>, x: i32) -> i32 {
            if p == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return match x {
                (if true { slot = p; 0 } else { 0 })..(*slot) => *slot,
                _ => *slot
            };
        }
    )"));
    CHECK(contains_diagnostic_code(
        analyze_test_errors(R"(
        enum E { Number(i32), Empty }
        fn f(p: ptr<i32>, x: E) -> i32 {
            if p == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return match x {
                .Number((if true { slot = p; 0 } else { 0 })..10) => *slot,
                _ => *slot
            };
        }
    )"),
        DiagnosticCode::PointerNonNull
    ));
}
