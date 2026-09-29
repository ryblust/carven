module carven:test.internal.compiler.diagnostics.failures;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler diagnostics: failure copyability closes after nominal signatures",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual(
                "forward-failure.cv",
                "fn direct() throw Later {} "
                "fn nested() throw Wrapper {} "
                "struct Wrapper { cause: Later } "
                "struct Later {}"
            );
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("forward.failure"),
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:failures").value(),
                }
            );

            ct::expect(result.has_value());
        }
    );

    ct::test(
        "Compiler diagnostics: catch reachability has one precisely owned subject",
        [] static noexcept {
            struct WarningExpectation final {
                std::string_view source;
                DiagnosticCode code;
                std::string_view primary_text;
            };

            const auto cases = std::array {
                WarningExpectation {
                    .source = "struct Alpha {} struct Beta {} "
                              "private fn produce() -> i32 throw Alpha { throw Alpha {}; } "
                              "fn recover() -> i32 { return try { produce()? } catch { "
                              "Alpha(_) => 1, Alpha(_) | Beta(_) => 2, }; }",
                    .code = DiagnosticCode::EffectCatchArmUnreachable,
                    .primary_text = "Alpha(_) | Beta(_) => 2",
                },
                WarningExpectation {
                    .source = "struct Alpha {} struct Beta {} "
                              "private fn produce() -> i32 throw Alpha { throw Alpha {}; } "
                              "fn recover() -> i32 { return try { produce()? } catch { "
                              "Alpha(_) | Beta(_) => 1, }; }",
                    .code = DiagnosticCode::EffectCatchAlternativeUnreachable,
                    .primary_text = "Beta(_)",
                },
            };

            ct::each(cases, &WarningExpectation::source, [&](const auto& expectation) noexcept {
                auto sources = SourceManager();
                const auto source_id =
                    *sources.append_virtual("catch-warning.cv", std::string(expectation.source));
                const auto input = SourceModuleInput {
                    .source_id = source_id,
                    .module_path = *CanonicalModulePath::from_value("catch_warning"),
                };
                const auto result = compile(
                    sources,
                    SourceBatch {.modules = std::span(&input, 1)},
                    TargetPlanningRequest {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain =
                            LinkageDomain::explicit_value("test:catch-warnings").value(),
                    }
                );

                ct::expect(result.has_value());
                if (!result.has_value()) {
                    return;
                }
                ct::expect_equal(
                    std::ranges::count_if(
                        result->diagnostics,
                        [&](const Diagnostic& diagnostic) noexcept {
                            return diagnostic.finding.code == expectation.code;
                        }
                    ),
                    1
                );
                const auto* warning = ct::find_diagnostic(result->diagnostics, expectation.code);
                if (warning == nullptr) {
                    return;
                }
                if (!ct::expect(warning->attachment.primary.has_value())) {
                    return;
                }
                ct::expect_equal(
                    sources.slice(warning->attachment.primary->span),
                    expectation.primary_text
                );
            });
        }
    );

    ct::test("Compiler diagnostics: control and fixed-point failures remain semantic contracts", [] static noexcept {
        static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
            {
                .name = "array of slices rejects callable failure narrowing",
                .source =
                    "struct Failure {} fn invalid(source: [[fn() -> i32 throw Failure]; 0]) { let adopted: [[fn() -> i32]; 0] = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "array of slices rejects callable failure widening",
                .source =
                    "struct Failure {} fn invalid(source: [[fn() -> i32]; 1]) { let adopted: [[fn() -> i32 throw Failure]; 1] = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "array callable failure narrowing",
                .source =
                    "struct Failure {} fn invalid(source: [fn() -> i32 throw Failure; 1]) { let adopted: [fn() -> i32; 1] = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "empty array callable failure narrowing",
                .source =
                    "struct Failure {} fn invalid(source: [fn() -> i32 throw Failure; 0]) { let adopted: [fn() -> i32; 0] = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "nested array callable failure narrowing",
                .source =
                    "struct Failure {} fn invalid(source: [[fn() -> i32 throw Failure; 1]; 1]) { let adopted: [[fn() -> i32; 1]; 1] = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "callable parameter failure invariance",
                .source =
                    "struct Failure {} fn invalid(source: fn(fn() -> i32 throw Failure) -> i32) { let adopted: fn(fn() -> i32) -> i32 = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "callable parameter failure invariance in arrays",
                .source =
                    "struct Failure {} fn invalid(source: [fn(fn() -> i32) -> i32; 0]) { let adopted: [fn(fn() -> i32 throw Failure) -> i32; 0] = source; }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "void return rejects a data result",
                .source = "fn invalid() -> void { return 42; }",
                .code = DiagnosticCode::TypeReturnValue,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "value return rejects void",
                .source = "fn action() {} fn invalid() -> i32 { return action(); }",
                .code = DiagnosticCode::TypeMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "void cannot initialize a binding",
                .source = "fn action() {} fn invalid() { let value = action(); }",
                .code = DiagnosticCode::TypeValueRequired,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "void forwarding requires failure consumption",
                .source =
                    "struct Failure {} fn action() throw Failure { throw Failure {}; } fn invalid() throw Failure { return action(); }",
                .code = DiagnosticCode::EffectUnmarked,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "missing return",
                .source = "fn value() -> i32 {}",
                .code = DiagnosticCode::FlowMissingReturn,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "nonconstant binding",
                .source = "fn invalid(input: i32) { const value = input; }",
                .code = DiagnosticCode::ConstInitializer,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "nonexhaustive match",
                .source = "fn invalid(value: i32) { match value { 1 => {}, } }",
                .code = DiagnosticCode::MatchNonExhaustive,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "pattern binding mismatch",
                .source = "enum Value { Integer(i32), Flag(bool) } "
                          "fn invalid(value: Value) -> i32 { return match value { "
                          ".Integer(item) | .Flag(_) => 1, _ => 0, }; }",
                .code = DiagnosticCode::MatchBindingMismatch,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "duplicate pattern binding",
                .source = "enum Value { Pair(i32, i32) } "
                          "fn invalid(value: Value) -> i32 { return match value { "
                          ".Pair(item, item) => item, }; }",
                .code = DiagnosticCode::NameDuplicateLocal,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "subsumed or-pattern alternative",
                .source = "enum Value { A, B } "
                          "fn invalid(value: Value) { match value { .A | _ => {}, } }",
                .code = DiagnosticCode::MatchDuplicateAlternative,
                .primary_text = ".A",
            },
            {
                .name = "duplicate catch alternative",
                .source = "struct Failure {} "
                          "fn fail() -> i32 throw Failure { throw Failure {}; } "
                          "fn invalid() -> i32 { return try { fail()? } catch { "
                          "Failure(_) | Failure(_) => 0, }; }",
                .code = DiagnosticCode::MatchDuplicateAlternative,
                .primary_text = "Failure(_)",
            },
            {
                .name = "recursive value storage",
                .source = "enum Recursive { Next(Recursive), End }",
                .code = DiagnosticCode::TypeRecursiveStorage,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "published surface visibility leak",
                .source = "struct Hidden {} export enum Public { Value(Hidden), Empty }",
                .code = DiagnosticCode::TypeVisibilityLeak,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "module-domain surface visibility leak",
                .source = "private struct Hidden {} fn shared() -> Hidden { return Hidden {}; }",
                .code = DiagnosticCode::TypeVisibilityLeak,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "nested callable surface visibility leak",
                .source = "private struct Hidden {} "
                          "export fn shared(callback: fn(Hidden) -> i32) {}",
                .code = DiagnosticCode::TypeVisibilityLeak,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "failure surface visibility leak",
                .source = "private struct Hidden {} export fn shared() throw Hidden {}",
                .code = DiagnosticCode::TypeVisibilityLeak,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "unmarked inferred failure",
                .source = "struct Failure {} "
                          "private fn caller() -> i32 { return failing(); } "
                          "private fn failing() -> i32 { throw Failure {}; }",
                .code = DiagnosticCode::EffectUnmarked,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "published callable requires a declared failure contract",
                .source = "struct Failure {} fn failing() { throw Failure {}; }",
                .code = DiagnosticCode::EffectThrowPublished,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "declared failure contract does not expand",
                .source = "struct First {} struct Second {} "
                          "fn bounded() throw First { throw Second {}; }",
                .code = DiagnosticCode::EffectSignatureBound,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "partial catch",
                .source = "enum Failure { First(i32), Second(i32) } "
                          "fn fail() -> i32 throw Failure { return 0; } "
                          "fn invalid() -> i32 { return try { fail()? } catch { "
                          "Failure(.First(value)) => value, }; }",
                .code = DiagnosticCode::EffectCatchNonExhaustive,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "escaping capturing closure",
                .source = "fn invalid() { let value = 1; "
                          "let callback: fn(i32) -> i32 = "
                          "[value](input: i32) { return input + value; }; }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "stored callable view",
                .source = "struct Invalid { callback: fn(i32) -> i32 }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "returned callable view",
                .source = "fn invalid(callback: fn(i32) -> i32) -> fn(i32) -> i32 { "
                          "return callback; }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "inferred lambda return join cannot escape as a callable view",
                .source = "fn first(value: i32) -> i32 { return value; } "
                          "fn second(value: i32) -> i32 { return value + 1; } "
                          "fn invalid() { let choose = [](flag: bool) { "
                          "if flag { return first; } return second; }; }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "widened view cannot outlive its source view storage",
                .source = "struct First {} struct Second {} "
                          "fn narrow(value: i32) -> i32 throw First { return value; } "
                          "fn invalid() { "
                          "var outer: fn(i32) -> i32 throw First + Second = narrow; "
                          "if true { let inner: fn(i32) -> i32 throw First = narrow; "
                          "outer = inner; } }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "taken stored view cannot be failure-widened",
                .source = "struct First {} struct Second {} "
                          "fn narrow(value: i32) -> i32 throw First { return value; } "
                          "fn invalid() { "
                          "let source: fn(i32) -> i32 throw First = narrow; "
                          "let widened: fn(i32) -> i32 throw First + Second = &&source; }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "Write-borrow callable storage rejects a capturing target",
                .source = "fn invalid(&destination: fn(i32) -> i32) { "
                          "let offset = 1; "
                          "let owner = [offset](value: i32) { return value + offset; }; "
                          "destination = owner; }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "captured callable view",
                .source = "fn invalid(callback: fn(i32) -> i32) { "
                          "let wrapper = [callback](value: i32) { return callback(value); }; "
                          "let result = wrapper(1); }",
                .code = DiagnosticCode::TypeCallableViewEscape,
                .primary_text = NonemptyPrimarySpan {},
            },
            {
                .name = "Take conflicts with an active callable-view loan",
                .source = "fn invalid() { let source = 1; "
                          "let owner = [source](value: i32) { return value + source; }; "
                          "let view: fn(i32) -> i32 = owner; let moved = &&owner; "
                          "let result = view(1); }",
                .code = DiagnosticCode::AccessBorrowConflict,
                .primary_text = "&&",
            },
        });

        check_compiler_errors(cases);
    });

    ct::test(
        "Compiler diagnostics: explicit entry contracts and implicit entry inference",
        [] static noexcept {
            struct Case final {
                std::string_view source;
                std::optional<DiagnosticCode> error;
            };

            const auto cases = std::array {
                Case {"fn main() {}", std::nullopt},
                Case {"fn main() -> i32 { return 7; }", std::nullopt},
                Case {"struct E {} fn main() throw E { throw E {}; }", std::nullopt},
                Case {"struct E {} private fn main() throw E {}", std::nullopt},
                Case {"struct E {} throw E {};", std::nullopt},
                Case {
                    "struct E {} private fn fail() throw E { throw E {}; } fail()?;",
                    std::nullopt
                },
                Case {
                    "struct E {} private fn fail() throw E { throw E {}; } "
                    "try { fail()?; } catch { E(_) => {}, }",
                    std::nullopt
                },
                Case {
                    "struct E {} private fn main() { throw E {}; }",
                    DiagnosticCode::EffectThrowPublished
                },
                Case {
                    "struct E {} fn main() { throw E {}; }",
                    DiagnosticCode::EffectThrowPublished
                },
                Case {
                    "struct E {} struct F {} fn main() throw E { throw F {}; }",
                    DiagnosticCode::EffectSignatureBound
                },
                Case {
                    "struct E {} test \"root\" { throw E {}; }",
                    DiagnosticCode::EffectRootUnhandled
                },
            };
            ct::each(cases, &Case::source, [&](const auto& item) noexcept {
                auto sources = SourceManager();
                const auto source = *sources.append_virtual("entry.cv", std::string(item.source));
                const auto input = SourceModuleInput {
                    .source_id = source,
                    .module_path = *CanonicalModulePath::from_value("entry"),
                };
                const auto result = compile(
                    sources,
                    SourceBatch {.modules = std::span(&input, 1)},
                    TargetPlanningRequest {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain = *LinkageDomain::explicit_value("test:entry"),
                    }
                );
                if (!item.error.has_value()) {
                    ct::expect(result.has_value());
                } else {
                    if (!(ct::expect(!(result.has_value())))) {
                        return;
                    }
                    if (!(ct::expect_equal(result.error().size(), 1uz))) {
                        return;
                    }
                    ct::expect_diagnostic(result.error(), *item.error);
                    ct::expect_equal(result.error().front().finding.code, *item.error);
                }
            });
        }
    );

    ct::test(
        "Compiler diagnostics: failure explanations describe resolved source contracts",
        [] static noexcept {
            struct Case final {
                std::string_view source;
                DiagnosticCode code;
                std::string_view message;
                std::vector<std::string> notes;
            };

            const auto cases = std::array {
                Case {
                    "fn f() -> i32 { return 1?; }",
                    DiagnosticCode::EffectPropagateRedundant,
                    "'?' requires a fallible expression",
                    {}
                },
                Case {
                    "fn plain() -> i32 { return 1; } fn f() -> i32 { return plain()?; }",
                    DiagnosticCode::EffectPropagateRedundant,
                    "'?' requires a fallible expression",
                    {}
                },
                Case {
                    "struct Z {} struct A {} fn f() throw Z + A {} "
                    "fn main() { try { f()?; } catch {} }",
                    DiagnosticCode::EffectCatchNonExhaustive,
                    "catch does not cover every protected failure",
                    {"failure type not fully covered: app.A",
                     "failure type not fully covered: app.Z"}
                },
                Case {
                    "struct A {} struct B {} fn f() throw A + B {} "
                    "fn main() { try { f()?; } catch { A(_) => {} } }",
                    DiagnosticCode::EffectCatchNonExhaustive,
                    "catch does not cover every protected failure",
                    {"failure type not fully covered: app.B"}
                },
                Case {
                    "enum E { A, B } fn f() throw E {} "
                    "fn main() { try { f()?; } catch { E(.A) => {} } }",
                    DiagnosticCode::EffectCatchNonExhaustive,
                    "catch does not cover every protected failure",
                    {"failure type not fully covered: app.E"}
                },
                Case {
                    "struct E {} fn f() throw E {} fn guard() -> bool { return false; } "
                    "fn main() { try { f()?; } catch { E(_) if guard() => {} } }",
                    DiagnosticCode::EffectCatchNonExhaustive,
                    "catch does not cover every protected failure",
                    {"failure type not fully covered: app.E"}
                },
            };
            ct::each(cases, &Case::source, [&](const auto& item) noexcept {
                auto sources = SourceManager();
                const auto source = *sources.append_virtual("app.cv", std::string(item.source));
                const auto input = SourceModuleInput {
                    .source_id = source,
                    .module_path = *CanonicalModulePath::from_value("app"),
                };
                const auto result = compile(
                    sources,
                    SourceBatch {.modules = std::span(&input, 1)},
                    TargetPlanningRequest {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain =
                            *LinkageDomain::explicit_value("test:failure-explanations"),
                    }
                );
                if (!(ct::expect(!(result.has_value())))) {
                    return;
                }
                const auto* diagnostic = ct::find_diagnostic(result.error(), item.code);
                if (!(ct::expect(diagnostic != nullptr))) {
                    return;
                }
                ct::expect_equal(diagnostic->finding.message, item.message);
                if (!(ct::expect(diagnostic->attachment.primary.has_value()))) {
                    return;
                }
                ct::expect((diagnostic->attachment.primary->span.source_id == source));
                auto notes = std::vector<std::string>();
                for (const auto& note : diagnostic->attachment.notes) {
                    notes.push_back(note.message);
                }
                ct::expect(notes == item.notes);
            });
        }
    );

    ct::test(
        "Compiler diagnostics: catch type names distinguish modules in stable order",
        [] static noexcept {
            for (const auto reverse : {false, true}) {
                auto sources = SourceManager();
                const auto alpha = *sources.append_virtual(
                    "alpha.cv",
                    "export struct E {} export fn first() throw E {}"
                );
                const auto zeta = *sources.append_virtual(
                    "zeta.cv",
                    "export struct E {} export fn second() throw E {}"
                );
                const auto app = *sources.append_virtual(
                    "app.cv",
                    "import alpha using first; import zeta using second; "
                    "fn main() { try { second()?; first()?; } catch {} }"
                );
                auto inputs = std::array {
                    SourceModuleInput {
                        .source_id = zeta,
                        .module_path = *CanonicalModulePath::from_value("zeta")
                    },
                    SourceModuleInput {
                        .source_id = alpha,
                        .module_path = *CanonicalModulePath::from_value("alpha")
                    },
                    SourceModuleInput {
                        .source_id = app,
                        .module_path = *CanonicalModulePath::from_value("app")
                    },
                };
                if (reverse) {
                    std::ranges::reverse(inputs);
                }
                const auto result = compile(
                    sources,
                    SourceBatch {.modules = inputs},
                    TargetPlanningRequest {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain = *LinkageDomain::explicit_value("test:catch-type-names"),
                    }
                );
                if (!ct::expect(!(result.has_value()))) {
                    return;
                }
                const auto* diagnostic =
                    ct::find_diagnostic(result.error(), DiagnosticCode::EffectCatchNonExhaustive);
                if (!ct::expect(diagnostic != nullptr)) {
                    return;
                }
                if (!ct::expect_equal(diagnostic->attachment.notes.size(), 2uz)) {
                    return;
                }
                ct::expect_equal(
                    diagnostic->attachment.notes[0].message,
                    std::string_view("failure type not fully covered: alpha.E")
                );
                ct::expect_equal(
                    diagnostic->attachment.notes[1].message,
                    std::string_view("failure type not fully covered: zeta.E")
                );
            }
        }
    );

    ct::test(
        "Compiler: array adoption preserves equal nested callable contracts",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual(
                "adoption.cv",
                "struct A {} struct B {} "
                "fn higher(source: [fn(fn() -> i32 throw A + B) -> i32 throw A; 1]) { "
                "let target: [fn(fn() -> i32 throw B + A) -> i32 throw A + B; 1] = source; } "
                "fn slices(source: [[fn() -> i32 throw A + B]; 0]) { "
                "let target: [[fn() -> i32 throw B + A]; 0] = source; }"
            );
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("adoption"),
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:adoption").value(),
                }
            );
            ct::expect(result.has_value());
        }
    );

    ct::test(
        "Compiler diagnostics: duplicate catch-all alternatives need no known failure type",
        [] static noexcept {
            check_compiler_error(
                "fn f() { try {} catch { _ | _ => {}, } }",
                DiagnosticCode::MatchDuplicateAlternative,
                std::string_view("_")
            );
        }
    );
});

} // namespace
