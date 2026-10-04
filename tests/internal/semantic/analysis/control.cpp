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
import :semantic.semir.traversal;
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

        ct::expect_diagnostic(
            analyze_test_errors(
                "private fn known() -> i32 {\n"
                "    if true { return 1; } else { let ignored = 0; }\n"
                "}\n"
            ),
            DiagnosticCode::FlowMissingReturn
        );

        const auto live_fallthrough = analyze_test_errors(
            "private fn invalid(value: bool) -> i32 {\n"
            "    if value { return 1; }\n"
            "}\n"
        );
        ct::expect_diagnostic(live_fallthrough, DiagnosticCode::FlowMissingReturn);
    });

    ct::test("Semantic control: a known condition does not select completion", [] static noexcept {
        const auto conditions = std::to_array<std::string_view>({
            "flag",
            "!flag",
            "flag || true",
            "flag && false",
            "true",
            "(2 + 3) == 5",
            "[false, true][1]",
        });
        for (const auto keyword : {"let", "const"}) {
            ct::each(
                conditions,
                [](auto condition) static noexcept { return condition; },
                [&](auto condition) noexcept {
                    ct::expect_diagnostic(
                        analyze_test_errors(
                            std::format(
                                "fn probe() -> i32 {{ {} flag = true; if {} {{ return 1; }} }}",
                                keyword,
                                condition
                            )
                        ),
                        DiagnosticCode::FlowMissingReturn
                    );
                    ct::expect_diagnostic(
                        analyze_test_errors(
                            std::format(
                                "fn probe() -> i32 {{ {} flag = true; while {} {{}} }}",
                                keyword,
                                condition
                            )
                        ),
                        DiagnosticCode::FlowMissingReturn
                    );
                }
            );
        }
        static_cast<void>(analyze_test_program("fn probe() -> i32 { while {} }"));
        ct::expect_diagnostic(
            analyze_test_errors("fn probe() -> i32 { while { break; } }"),
            DiagnosticCode::FlowMissingReturn
        );
        const auto literal = analyze_test_errors("fn probe() -> i32 { while true { return 1; } }");
        const auto* missing = ct::find_diagnostic(literal, DiagnosticCode::FlowMissingReturn);
        if (ct::expect(missing != nullptr)) {
            ct::expect_equal(missing->attachment.helps.size(), 1uz);
        }
    });

    ct::test(
        "Semantic control: static slice projection preserves index checks",
        [] static noexcept {
            const auto program = analyze_test_program(
                "const values: [i32] = [3, 7]; "
                "fn valid() -> i32 => values[1]; "
                "fn checked() -> i32 => values[4];"
            );
            const auto callables = test_function_callables(program);
            if (!ct::expect_equal(callables.size(), 2uz)) {
                return;
            }
            for (auto position = 0uz; position < callables.size(); ++position) {
                ct::scenario(std::format("index {}", position), [&]() noexcept {
                    const auto body_id =
                        callable_body_id(program.declarations().callable(callables[position]));
                    if (!ct::expect(body_id.has_value())) {
                        return;
                    }
                    const auto& body = program.bodies().body(*body_id);
                    if (!ct::expect_equal(body.region().statements.size(), 1uz)) {
                        return;
                    }
                    const auto* returned =
                        std::get_if<SemReturn>(&body.region().statements.front().value);
                    if (!ct::expect(returned != nullptr && returned->value.has_value())) {
                        return;
                    }
                    const auto& expression = *returned->value;
                    const auto* index = std::get_if<SemIndex>(&expression.value);
                    if (!ct::expect(index != nullptr)) {
                        return;
                    }
                    ct::expect(std::holds_alternative<RuntimeCheckedBounds>(index->bounds));
                    ct::expect(expression.constant.has_value() == (position == 0uz));
                });
            }
        }
    );

    ct::test(
        "Semantic pointers: aggregate snapshots and Take retain local null proofs",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
                struct PointerSnapshot { pointer: ptr<&i32> }
                fn probe(p: ptr<&i32>) -> i32 {
                    if p == nullptr { return 0; }
                    let indexed = [p][0usize];
                    let field = (PointerSnapshot { pointer: indexed }).pointer;
                    let narrowed: ptr<i32> = field;
                    let transferred = &&narrowed;
                    return *transferred;
                }
            )");
            const auto callables = test_function_callables(program);
            if (!ct::expect_equal(callables.size(), 1uz)) {
                return;
            }
            const auto body_id =
                callable_body_id(program.declarations().callable(callables.front()));
            if (!ct::expect(body_id.has_value())) {
                return;
            }
            auto observed_value_index = false;
            visit_semantic_nodes(
                program.bodies().body(*body_id).region(),
                [&](const SemanticExpression& expression) noexcept {
                    const auto* index = std::get_if<SemIndex>(&expression.value);
                    if (index == nullptr
                        || !std::holds_alternative<SemArray>(index->source->value)) {
                        return;
                    }
                    observed_value_index = true;
                    ct::expect(index->source->category == SemanticValueCategory::Value);
                    if (!ct::expect(index->index->constant.has_value())) {
                        return;
                    }
                    const auto* integer = std::get_if<IntegerConstant>(
                        &program.constants().constant(*index->index->constant).value
                    );
                    if (!ct::expect(integer != nullptr)) {
                        return;
                    }
                    const auto position = integer->as_unsigned();
                    if (ct::expect(position.has_value())) {
                        ct::expect_equal(*position, 0u);
                    }
                }
            );
            ct::expect_equal(observed_value_index, true);

            ct::expect_diagnostic(
                analyze_test_errors(R"(
                    struct PointerSnapshot { pointer: ptr<&i32> }
                    fn probe(p: ptr<&i32>) -> i32 {
                        let indexed = [p][0usize];
                        let field = (PointerSnapshot { pointer: indexed }).pointer;
                        let narrowed: ptr<i32> = field;
                        let transferred = &&narrowed;
                        return *transferred;
                    }
                )"),
                DiagnosticCode::PointerNonNull
            );
        }
    );

    ct::test("Semantic control: bindings cannot select pointer paths", [] static noexcept {
        ct::expect_diagnostic(
            analyze_test_errors(
                "fn probe() -> i32 { var p: ptr<i32> = nullptr; let flag = false; "
                "if flag { return *p; } return 0; }"
            ),
            DiagnosticCode::PointerNonNull
        );
        ct::expect_diagnostic(
            analyze_test_errors(
                "fn probe() -> i32 { var p: ptr<i32> = nullptr; let flag = true; "
                "if flag && false { return *p; } return 0; }"
            ),
            DiagnosticCode::PointerNonNull
        );
        ct::expect_diagnostic(
            analyze_test_errors(
                "fn probe() -> i32 { var p: ptr<i32> = nullptr; const flag = false; "
                "if flag { return *p; } return 0; }"
            ),
            DiagnosticCode::PointerNonNull
        );
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
            ct::expect_diagnostic(
                analyze_test_errors(
                    "fn invalid() { var x = 1; if false { let moved = &&x; } let result = x; }"
                ),
                DiagnosticCode::AccessUnavailable
            );
        }
    );

    ct::test(
        "Semantic control: irrefutable guard rejection retains completed writes",
        [] static noexcept {
            static_cast<void>(analyze_test_program(
                "struct E {} fn fail() throw E { throw E {}; } "
                "fn valid() { var x = 1; let moved = &&x; "
                "match true { _ if if true { x = 2; false } else { x = 2; false } => {}, "
                "_ => { let read = x; }, } "
                "let again = &&x; try { fail()?; } catch { "
                "E(_) if if true { x = 3; false } else { x = 3; false } => {}, "
                "E(_) => { let read = x; }, } }"
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

    ct::test("Semantic control: published effects include every branch", [] static noexcept {
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
            ct::expect(returned->value->exits_test);
            ct::expect(program.may_stop_test(callables[index]));
        }
        for (const auto entry : program.tests().entries()) {
            const auto& body = program.bodies().body(*entry.value.body);
            const auto* initialized =
                std::get_if<SemInitialize>(&body.region().statements.front().value);
            if (!ct::expect(initialized != nullptr)) {
                return;
            }
            ct::expect(initialized->initializer.exits_test);
        }
    });

    ct::test("Semantic control: residual effects follow static selection", [] static noexcept {
        const auto program = analyze_test_program(R"(
        fn selected() -> i32 {
            return const if false { require(false); 1 }
                         else { let value = 2; value };
        }
        fn ordinary() -> i32 {
            return if false { require(false); 1 }
                   else { let value = 2; value };
        }
        fn selected_caller() -> i32 { return selected(); }
        fn ordinary_caller() -> i32 { return ordinary(); }
    )");
        const auto callables = test_function_callables(program);
        const auto expected = std::array {false, true, false, true};
        if (!ct::expect_equal(callables.size(), expected.size())) {
            return;
        }
        for (const auto [index, callable] : std::views::enumerate(callables)) {
            const auto& body =
                program.bodies().body(*program.declarations().body_for_callable(callable));
            if (!ct::expect(!body.region().statements.empty())) {
                return;
            }
            const auto* returned = std::get_if<SemReturn>(&body.region().statements.front().value);
            if (!ct::expect(returned != nullptr && returned->value.has_value())) {
                return;
            }
            ct::expect_equal(returned->value->exits_test, expected[index]);
            ct::expect_equal(body.region().exits_test, expected[index]);
            ct::expect_equal(program.may_stop_test(callable), expected[index]);
        }
    });

    ct::test(
        "Semantic control: native invocations inherit argument completion",
        [] static noexcept {
            const auto invocations = std::array {
                "::native_call(if flag {{ fail(\"stop\"); }} else {{ {} }})",
                "::Native {{ if flag {{ fail(\"stop\"); }} else {{ {} }} }}",
            };
            for (const auto invocation : invocations) {
                const auto body = [&](std::string_view alternative) noexcept {
                    return std::format(
                        "private fn run(flag: bool) -> i32 {{ {}; }}",
                        std::vformat(invocation, std::make_format_args(alternative))
                    );
                };
                static_cast<void>(analyze_test_program(body("fail(\"stop\"); 1")));
                ct::expect_diagnostic(
                    analyze_test_errors(body("1")),
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
                (if true { slot = p; 0 } else { slot = p; 0 })..0 | 0..(*slot) => *slot,
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
                .Number((if true { slot = p; 0 } else { slot = p; 0 })..(*slot)) => *slot,
                _ => *slot
            };
        }
    )"));

            static_cast<void>(analyze_test_program(R"(
        fn f(p: ptr<i32>, x: i32) -> i32 {
            if p == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return match x {
                (if true { slot = p; 0 } else { slot = p; 0 })..(*slot) => *slot,
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
                .Number((if true { slot = p; 0 } else { slot = p; 0 })..10) => *slot,
                _ => *slot
            };
        }
    )"),
                DiagnosticCode::PointerNonNull
            );
        }
    );

    ct::test(
        "Semantic patterns: tag rejection preserves distinct ownership joins",
        [] static noexcept {
            const auto body = R"(
        fn probe(subject: Choice) {
            var value = 1;
            let moved = &&value;
            match subject {
                .Number(_, (if true { value = 2; 0 } else { value = 2; 0 })..10) => {},
                _ => {},
            }
            let read = value;
        }
    )";
            static_cast<void>(
                analyze_test_program(std::string("enum Choice { Number(i32, i32) } ") + body)
            );
            ct::expect_diagnostic(
                analyze_test_errors(std::string("enum Choice { Number(i32, i32), Empty } ") + body),
                DiagnosticCode::AccessUnavailable
            );
        }
    );

    ct::test(
        "Semantic patterns: exhaustive payloads preserve later bound pointer facts",
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string_view declarations;
                std::string_view pattern;
                bool exhaustive;
            };
            const auto inputs = std::array {
                Input {
                    .name = "complete boolean coverage",
                    .declarations = "enum Choice { Number(bool, i32) }",
                    .pattern = "true | false",
                    .exhaustive = true,
                },
                Input {
                    .name = "partial boolean coverage",
                    .declarations = "enum Choice { Number(bool, i32) }",
                    .pattern = "true",
                    .exhaustive = false,
                },
                Input {
                    .name = "complete bounded integer coverage",
                    .declarations = "enum Choice { Number(u8, i32) }",
                    .pattern = "0..=255",
                    .exhaustive = true,
                },
                Input {
                    .name = "complete open integer coverage",
                    .declarations = "enum Choice { Number(u8, i32) }",
                    .pattern = "0..",
                    .exhaustive = true,
                },
                Input {
                    .name = "partial integer coverage",
                    .declarations = "enum Choice { Number(u8, i32) }",
                    .pattern = "0..255",
                    .exhaustive = false,
                },
                Input {
                    .name = "complete numeric enum coverage",
                    .declarations = "enum Kind { Only } enum Choice { Number(Kind, i32) }",
                    .pattern = ".Only",
                    .exhaustive = true,
                },
            };
            ct::each(inputs, &Input::name, [](const Input& input) static noexcept {
                const auto source = std::string(input.declarations) + R"(
        fn probe(subject: Choice, pointer: ptr<i32>, flag: bool) -> i32 {
            if pointer == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return match subject {
                .Number(
    )" + std::string(input.pattern)
                    + R"(,
                    (if flag { slot = pointer; 0 } else { slot = pointer; 0 })..10) => *slot,
                _ => *slot,
            };
        }
    )";
                if (input.exhaustive) {
                    static_cast<void>(analyze_test_program(source));
                } else {
                    ct::expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::PointerNonNull
                    );
                }
            });
        }
    );

    ct::test(
        "Semantic patterns: outward bound exits skip remaining pointer reads",
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
        fn probe(subject: i32, pointer: ptr<i32>) -> i32 {
            return match subject {
                (if true { fail(); 0 } else { fail(); 0 })..(*pointer) | 0..(*pointer) => 1,
                _ => 2,
            };
        }
    )"));
        }
    );

    ct::test(
        "Semantic patterns: guard rejection retains established pointer facts",
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string_view pattern;
                bool exhaustive;
            };
            const auto inputs = std::array {
                Input {
                    .name = "complete catch alternatives",
                    .pattern = "Failure(.One) | Failure(.Two)",
                    .exhaustive = true,
                },
                Input {
                    .name = "partial catch alternatives",
                    .pattern = "Failure(.One)",
                    .exhaustive = false,
                },
            };
            ct::each(inputs, &Input::name, [](const Input& input) static noexcept {
                const auto source = std::string(R"(
        enum Failure { One, Two }
        fn raise(flag: bool) throw Failure {
            if flag { throw Failure::One; } else { throw Failure::Two; }
        }
        fn probe(pointer: ptr<i32>, flag: bool) -> i32 {
            if pointer == nullptr { return 0; }
            var slot: ptr<i32> = nullptr;
            return try { raise(flag)?; 0 } catch {
    )") + std::string(input.pattern)
                    + R"(
                if if flag { slot = pointer; false } else { slot = pointer; false } => 1,
                Failure(_) => *slot,
            };
        }
    )";
                if (input.exhaustive) {
                    static_cast<void>(analyze_test_program(source));
                } else {
                    ct::expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::PointerNonNull
                    );
                }
            });
        }
    );

    ct::test("Semantic control: report messages contribute their test stops", [] static noexcept {
        const auto program = analyze_test_program(R"(
        fn message() -> str { fail(); }
        fn quiet() { assert(true, message()); check(true, message()); require(true, message()); }
        fn forward() { quiet(); }
        fn conditional(flag: bool) { assert(flag, message()); }
    )");
        const auto callables = test_function_callables(program);
        const auto expected = std::array {true, true, true, true};
        if (!ct::expect(callables.size() == expected.size())) {
            return;
        }
        for (auto index = 0uz; index < expected.size(); ++index) {
            ct::expect(program.may_stop_test(callables[index]) == expected[index])
                .note("index = ", index);
        }
    });

    ct::test(
        "Semantic control: only fail completes a report without returning",
        [] static noexcept {
            for (const auto operation : {"assert", "require", "check"}) {
                for (const auto condition : {"false", "true"}) {
                    ct::expect_diagnostic(
                        analyze_test_errors(
                            std::format(
                                "private fn open() -> i32 {{ {}({}); }}",
                                operation,
                                condition
                            )
                        ),
                        DiagnosticCode::FlowMissingReturn
                    )
                        .note("operation = ", operation);
                }
            }
            static_cast<void>(analyze_test_program("private fn stopped() -> i32 { fail(); }"));
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
