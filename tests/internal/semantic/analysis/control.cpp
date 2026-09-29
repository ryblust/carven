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
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Semantic control: try around an infallible body is silent", [] static noexcept {
        const auto diagnostics = analyze_test_errors(
            "struct Failure {}\n"
            "fn valid() { try {} catch { Failure(_) => {}, } }\n"
        );
        ct::expect(diagnostics.empty());
    });

    ct::test("Semantic control: only executable fallthrough requires a return", [] static noexcept {
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
        ct::expect_diagnostic(live_fallthrough, DiagnosticCode::FlowMissingReturn);
    });

    ct::test("Semantic structure: nested lambda owns a distinct body", [] static noexcept {
        const auto program = analyze_test_program(
            "private fn outer() {\n"
            "    let callback = []() { let nested = 1; };\n"
            "}\n"
        );
        const auto callables = test_function_callables(program);
        if (!ct::expect_equal(callables.size(), 1uz)) {
            return;
        }
        if (!ct::expect_equal(std::ranges::distance(program.declarations().callables()), 2)) {
            return;
        }
        if (!ct::expect_equal(program.bodies().size(), 2uz)) {
            return;
        }

        const auto outer_callable = callables.front();
        const auto outer_body = callable_body_id(program.declarations().callable(outer_callable));
        if (!ct::expect(outer_body.has_value())) {
            return;
        }
        auto closure_callable = std::optional<CallableID>();
        for (const auto [id, declaration] : program.declarations().callables()) {
            if (std::holds_alternative<ClosureBodyImplementation>(declaration.implementation)) {
                closure_callable = id;
            }
        }
        if (!ct::expect(closure_callable.has_value())) {
            return;
        }
        const auto nested_body =
            callable_body_id(program.declarations().callable(*closure_callable));
        if (!ct::expect(nested_body.has_value())) {
            return;
        }
        ct::expect(*outer_body != *nested_body).note("outer and nested body IDs differ");
        ct::expect_equal(program.bodies().body(*outer_body).kind(), BodyKind::Function);
        ct::expect_equal(program.bodies().body(*nested_body).kind(), BodyKind::Closure);
    });

    ct::test(
        "Semantic source contracts: unreachable operations remain checked",
        [] static noexcept {
            ct::expect_diagnostic(
                analyze_test_errors("fn invalid() { let x = 1; if false { x = 2; } }"),
                DiagnosticCode::AccessImmutable
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn set(&x: i32) {} fn invalid() { let x = 1; if false { set(&x); } }"
                ),
                DiagnosticCode::AccessImmutable
            );
            ct::expect_diagnostic(
                analyze_test_errors("fn invalid() { var x = 1; if false { x = (&&x); } }"),
                DiagnosticCode::AccessOperationConflict
            );
            ct::expect_diagnostic(
                analyze_test_errors("fn invalid() { let result = if true { 1 } else { false }; }"),
                DiagnosticCode::TypeMismatch
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "struct E {} fn fail() -> bool throw E { throw E {}; } "
                    "fn invalid() { let value = false && fail(); }"
                ),
                DiagnosticCode::EffectUnmarked
            );
            static_cast<void>(analyze_test_program(
                "fn valid() { var x = 1; if false { let moved = &&x; } let result = x; }"
            ));
        }
    );

    ct::test(
        "Semantic control: irrefutable guard rejection retains completed writes",
        [] static noexcept {
            static_cast<void>(analyze_test_program(
                "struct E {} fn fail() throw E { throw E {}; } "
                "fn valid() { var x = 1; let moved = &&x; "
                "match true { _ if if true { x = 2; false } else { false } => {}, _ => { let read = x; }, } "
                "let again = &&x; try { fail()?; } catch { "
                "E(_) if if true { x = 3; false } else { false } => {}, E(_) => { let read = x; }, } }"
            ));
        }
    );

    ct::test(
        "Semantic control: every operand is checked after a nonreturning operand",
        [] static noexcept {
            const auto prefix =
                std::string("struct E {}\nfn add(a: i32, b: i32) -> i32 { return a + b; }\n");
            for (const auto* expression : {
                     "(if true { throw E {}; } else { throw E {}; }) + missing",
                     "add(if true { throw E {}; } else { throw E {}; }, missing)",
                 }) {
                const auto diagnostics = analyze_test_errors(
                    prefix + "fn run() -> i32 throw E { return " + expression + "; }"
                );
                ct::expect_diagnostic(diagnostics, DiagnosticCode::NameUnresolved);
            }
            const auto missing_marker = analyze_test_errors(prefix + R"(
        fn fail() -> i32 throw E { throw E {}; }
        fn run() -> i32 throw E {
            return add(if true { throw E {}; } else { throw E {}; }, fail());
        }
    )");
            ct::expect_diagnostic(missing_marker, DiagnosticCode::EffectUnmarked);
            const auto unknown_callee = analyze_test_errors(prefix + R"(
        fn run() -> i32 throw E {
            return (if true { throw E {}; } else { throw E {}; })(1);
        }
    )");
            ct::expect_diagnostic(unknown_callee, DiagnosticCode::TypeNotCallable);
            const auto unknown_subject = analyze_test_errors(prefix + R"(
        fn run() -> i32 throw E {
            return match (if true { throw E {}; } else { throw E {}; }) { _ => 1 };
        }
    )");
            ct::expect_diagnostic(unknown_subject, DiagnosticCode::TypeValueRequired);
        }
    );

    ct::test(
        "Semantic functions: expression result dependencies are independent of module order",
        [] static noexcept {
            for (const auto reverse : {false, true}) {
                auto sources = SourceManager();
                const auto consumer = sources.append_virtual(
                    "consumer.cv",
                    "import producer using next; fn first() => next(4); fn again() => first();"
                );
                const auto producer =
                    sources.append_virtual("producer.cv", "export fn next(a: i32) => a + 1;");
                if (!ct::expect(consumer.has_value())) {
                    return;
                }
                if (!ct::expect(producer.has_value())) {
                    return;
                }
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
                if (!ct::expect(parsed.has_value())) {
                    return;
                }
                auto result = analyze(std::move(*parsed));
                if (!ct::expect(result.has_value())) {
                    return;
                }
                const auto& program = result->value;
                ct::expect_equal(program.bodies().size(), 3uz);
                for (const auto callable : test_function_callables(program)) {
                    const auto result_type = test_callable_signature(program, callable).result;
                    const auto expected = CanonicalTypeValue {BuiltinTypeValue {BuiltinType::I32}};
                    ct::expect(program.types().type(result_type).value == expected);
                }
            }
        }
    );

    ct::test(
        "Semantic functions: inferred expression results obey return restrictions",
        [] static noexcept {
            const auto view = analyze_test_errors("fn leak(value: fn() -> void) => value;");
            ct::expect_diagnostic(view, DiagnosticCode::TypeCallableViewEscape);
            const auto captures = analyze_test_errors(
                "fn leak() => []() { var local = 1; return [&local]() => local; }();"
            );
            ct::expect_diagnostic(captures, DiagnosticCode::AccessBorrowConflict);
            const auto propagation = analyze_test_errors(
                "struct E {} private fn fail() -> i32 throw E { throw E {}; } private fn bad() => fail();"
            );
            ct::expect_diagnostic(propagation, DiagnosticCode::EffectUnmarked);
        }
    );

    ct::test(
        "Semantic functions: expression body returns retain expansion provenance",
        [] static noexcept {
            const auto program = analyze_test_program("fn answer() => 42;");
            const auto callable = test_function_callables(program).front();
            const auto& body =
                program.bodies().body(*program.declarations().body_for_callable(callable));
            if (!ct::expect_equal(body.region().statements.size(), 1uz)) {
                return;
            }
            const auto& statement = body.region().statements.front();
            ct::expect(std::holds_alternative<SemReturn>(statement.value));
            const auto& origin = program.provenance().origin(statement.origin);
            if (!ct::expect(std::holds_alternative<ProgramExpansionOrigin>(origin.value))) {
                return;
            }
            ct::expect_equal(program.provenance().slice(statement.origin), std::string_view("=>"));
        }
    );

    ct::test(
        "Semantic control: test stop propagates through callable dependencies",
        [] static noexcept {
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
            if (!ct::expect(callables.size() == expected.size())) {
                return;
            }
            for (auto index = 0uz; index < expected.size(); ++index) {
                ct::expect(program.may_stop_test(callables[index]) == expected[index])
                    .note("index = ", index);
            }
        }
    );

    ct::test("Semantic control: published effects follow possible execution", [] static noexcept {
        const auto program = analyze_test_program(R"(
        fn direct() -> i32 { return if false { require(false); 1 } else { 2 }; }
        fn indirect() -> i32 { return if false { stop(); 1 } else { 2 }; }
        fn stop() { fail(); }
        test "nested calls" { let observed = if true { stop(); 1 } else { 2 }; check(observed == 1); }
    )");
        const auto callables = test_function_callables(program);
        if (!ct::expect(callables.size() == 3uz)) {
            return;
        }
        for (auto index = 0uz; index < 2uz; ++index) {
            const auto& body =
                program.bodies().body(*program.declarations().body_for_callable(callables[index]));
            const auto* returned = std::get_if<SemReturn>(&body.region().statements.front().value);
            if (!ct::expect(returned != nullptr)) {
                return;
            }
            if (!ct::expect(returned->value.has_value())) {
                return;
            }
            ct::expect(!returned->value->exits_test);
            ct::expect(!program.may_stop_test(callables[index]));
        }
        for (const auto entry : program.tests().entries()) {
            const auto& body = program.bodies().body(entry.value.body);
            const auto* initialized =
                std::get_if<SemInitialize>(&body.region().statements.front().value);
            if (!ct::expect(initialized != nullptr)) {
                return;
            }
            ct::expect(initialized->initializer.exits_test);
        }
    });

    ct::test(
        "Semantic control: native invocations inherit argument completion",
        [] static noexcept {
            const auto invocations = std::array {
                "::native_call(if {} {{ fail(\"stop\"); }} else {{ 1 }})",
                "::Native {{ if {} {{ fail(\"stop\"); }} else {{ 1 }} }}",
            };
            for (const auto invocation : invocations) {
                const auto body = [&](std::string_view condition) noexcept {
                    return std::format(
                        "private fn run() -> i32 {{ {}; }}",
                        std::vformat(invocation, std::make_format_args(condition))
                    );
                };
                static_cast<void>(analyze_test_program(body("true")));
                ct::expect_diagnostic(
                    analyze_test_errors(body("false")),
                    DiagnosticCode::FlowMissingReturn
                )
                    .note("invocation = ", invocation);
            }
        }
    );

    ct::test(
        "Semantic ranges: coverage partitions integer domains and nested payloads",
        [] static noexcept {
            static_cast<void>(analyze_test_program(
                "enum E { Value(u8), Empty }\n"
                "fn classify(x: E) -> i32 { return match x { .Value(0..128) => 0, "
                ".Value(128..=255) => 1, .Empty => 2 }; }\n"
            ));
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn f(x: u8) -> i32 { return match x { 0..127 => 0, 128..=255 => 1 }; }"
                ),
                DiagnosticCode::MatchNonExhaustive
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn f(x: i32, lo: i32) -> i32 { return match x { lo.. => 0 }; }"
                ),
                DiagnosticCode::MatchNonExhaustive
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn f(x: i32) -> i32 { return match x { 0..10 => 0, 10..20 => 1, 1..19 => 2, _ => 3 }; }"
                ),
                DiagnosticCode::FlowUnreachableMatchArm
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn f(x: i32) -> i32 { return match x { 0..10 | 4 => 0, _ => 1 }; }"
                ),
                DiagnosticCode::MatchDuplicateAlternative
            );
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn f(x: f64) -> i32 { return match x { 0..10 => 0, _ => 1 }; }"
                ),
                DiagnosticCode::TypeMatchPattern
            );
        }
    );

    ct::test(
        "Semantic ranges: selection protects the subject and excludes dead bound failures",
        [] static noexcept {
            const auto subject_write = analyze_test_errors(
                "fn change(&x: i32) -> i32 { x = 0; return 0; } "
                "fn f() { var x = 1; match x { (change(&x))..10 => {}, _ => {} } }"
            );
            ct::expect_diagnostic(subject_write, DiagnosticCode::AccessOperationConflict);
            const auto pattern_binding = analyze_test_errors(
                "enum E { Pair(i32, i32) } "
                "fn f(x: E) { match x { .Pair(lo, lo..10) => {}, _ => {} } }"
            );
            ct::expect_diagnostic(pattern_binding, DiagnosticCode::TypeMatchPattern);
            static_cast<void>(analyze_test_program(
                "struct E {} fn bound() -> i32 throw E { throw E {}; } "
                "fn f(x: i32) -> i32 { return match x { _ => 1, (bound()?)..10 => 2 }; }"
            ));
        }
    );

    ct::test(
        "Semantic ranges: bounds and element types obey integer contracts",
        [] static noexcept {
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
                Case {
                    "noninteger element",
                    "fn f(x: range<f64>) {}",
                    DiagnosticCode::TypeRangeInteger
                },
                Case {"missing element", "fn f(x: range) {}", DiagnosticCode::TypeUnresolved},
                Case {
                    "extra element",
                    "fn f(x: range<i32, u8>) {}",
                    DiagnosticCode::TypeUnresolved
                },
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
            ct::each(cases, &Case::name, [&](const auto& scenario) noexcept {
                ct::expect_diagnostic(
                    analyze_test_errors(std::string(scenario.source)),
                    scenario.code
                );
            });
            static_cast<void>(analyze_test_program("fn f() { for n: u8 in (0..=255) {} }"));
        }
    );

    ct::test(
        "Semantic ranges: bound sequencing preserves established pointer facts",
        [] static noexcept {
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
            ct::expect_diagnostic(
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
            );
        }
    );

    ct::test("Semantic control: successful reports exclude message test stops", [] static noexcept {
        const auto program = analyze_test_program(R"(
        fn message() -> str { fail(); }
        fn quiet() { assert(true, message()); check(true, message()); require(true, message()); }
        fn forward() { quiet(); }
        fn conditional(flag: bool) { assert(flag, message()); }
    )");
        const auto callables = test_function_callables(program);
        const auto expected = std::array {true, false, false, true};
        if (!ct::expect(callables.size() == expected.size())) {
            return;
        }
        for (auto index = 0uz; index < expected.size(); ++index) {
            ct::expect(program.may_stop_test(callables[index]) == expected[index])
                .note("index = ", index);
        }
    });

    ct::test(
        "Semantic control: report completion follows the selected continuation",
        [] static noexcept {
            for (const auto operation : {"assert", "require"}) {
                static_cast<void>(analyze_test_program(
                    std::format("private fn stopped() -> i32 {{ {}(false); }}", operation)
                ));
                ct::expect_diagnostic(
                    analyze_test_errors(
                        std::format("private fn open() -> i32 {{ {}(true); }}", operation)
                    ),
                    DiagnosticCode::FlowMissingReturn
                )
                    .note("operation = ", operation);
            }
            static_cast<void>(analyze_test_program(
                "private fn stopped() -> i32 { check(false, if true { fail(); } else { \"unused\" }); }"
            ));
        }
    );

    ct::test(
        "Semantic effects: immutable function targets retain their actual test-stop contract",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn plain() -> i32 => 1;
        fn stopping() -> i32 { require(false); return 2; }
        fn known_plain() -> i32 { let view: fn() -> i32 = plain; return view(); }
        fn known_stopping() -> i32 { let view: fn() -> i32 = stopping; return view(); }
        fn dynamic(view: fn() -> i32) -> i32 => view();
    )");
            const auto callables = test_function_callables(program);
            const auto expected = std::array {false, true, false, true, true};
            if (!ct::expect(callables.size() == expected.size())) {
                return;
            }
            for (const auto [index, callable] : std::views::enumerate(callables)) {
                ct::expect(program.may_stop_test(callable) == expected[index]);
            }
        }
    );
});

} // namespace
