module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.failures;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

struct ErrorExpectation final {
    std::string_view name;
    std::string_view source;
    std::string_view code;
    std::string_view primary_text;
};

} // namespace

TEST_CASE("Compiler diagnostics: failure copyability closes after nominal signatures") {
    auto sources = SourceManager();
    const auto source_id = *sources.append_virtual(
        "forward-failure.cv",
        "fn direct() throw Later {} "
        "fn nested() throw Wrapper {} "
        "struct Wrapper { cause: Later } "
        "struct Later {}"
    );
    const auto input = CompilationModuleInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("forward.failure"),
    };

    const auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:failures").value(),
        }
    );

    CHECK(result.has_value());
}

TEST_CASE("Compiler diagnostics: catch reachability has one precisely owned subject") {
    struct WarningExpectation final {
        std::string_view source;
        std::string_view code;
        std::string_view primary_text;
    };

    const auto cases = std::array {
        WarningExpectation {
            .source = "struct Alpha {} struct Beta {} "
                      "private fn produce() -> i32 throw Alpha { throw Alpha {}; } "
                      "fn recover() -> i32 { return try { produce()? } catch { "
                      "Alpha(_) => 1, Alpha(_) | Beta(_) => 2, }; }",
            .code = "CV-EFFECT-CATCH-ARM-UNREACHABLE",
            .primary_text = "Alpha(_) | Beta(_) => 2",
        },
        WarningExpectation {
            .source = "struct Alpha {} struct Beta {} "
                      "private fn produce() -> i32 throw Alpha { throw Alpha {}; } "
                      "fn recover() -> i32 { return try { produce()? } catch { "
                      "Alpha(_) | Beta(_) => 1, }; }",
            .code = "CV-EFFECT-CATCH-ALTERNATIVE-UNREACHABLE",
            .primary_text = "Beta(_)",
        },
    };

    for (const auto& expectation : cases) {
        auto sources = SourceManager();
        const auto source_id =
            *sources.append_virtual("catch-warning.cv", std::string(expectation.source));
        const auto input = CompilationModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("catch_warning"),
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:catch-warnings").value(),
            }
        );

        CHECK(result.has_value());
        if (!result.has_value()) {
            continue;
        }
        CHECK_EQ(
            std::ranges::count_if(
                result->diagnostics,
                [&](const Diagnostic& diagnostic) noexcept {
                    return diagnostic.finding.code == expectation.code;
                }
            ),
            1
        );
        const auto* warning = find_compiler_diagnostic(result->diagnostics, expectation.code);
        if (warning == nullptr) {
            continue;
        }
        REQUIRE(warning->attachment.primary.has_value());
        CHECK_EQ(sources.slice(warning->attachment.primary->span), expectation.primary_text);
    }
}

TEST_CASE("Compiler diagnostics: control and fixed-point failures remain semantic contracts") {
    static constexpr auto cases = std::to_array<ErrorExpectation>({
        {
            .name = "void return rejects a data result",
            .source = "fn invalid() { return 42; }",
            .code = "CV-TYPE-RETURN-VALUE",
            .primary_text = {},
        },
        {
            .name = "value return rejects void",
            .source = "fn action() {} fn invalid() -> i32 { return action(); }",
            .code = "CV-TYPE-MISMATCH",
            .primary_text = {},
        },
        {
            .name = "void cannot initialize a binding",
            .source = "fn action() {} fn invalid() { let value = action(); }",
            .code = "CV-TYPE-VALUE-REQUIRED",
            .primary_text = {},
        },
        {
            .name = "void forwarding requires failure consumption",
            .source =
                "struct Failure {} fn action() throw Failure { throw Failure {}; } fn invalid() throw Failure { return action(); }",
            .code = "CV-EFFECT-UNMARKED",
            .primary_text = {},
        },
        {
            .name = "missing return",
            .source = "fn value() -> i32 {}",
            .code = "CV-FLOW-MISSING-RETURN",
            .primary_text = {},
        },
        {
            .name = "nonconstant binding",
            .source = "fn runtime() -> i32 { return 1; } "
                      "fn invalid() { const value = runtime(); }",
            .code = "CV-CONST-INITIALIZER",
            .primary_text = {},
        },
        {
            .name = "nonexhaustive match",
            .source = "fn invalid(value: i32) { match value { 1 => {}, } }",
            .code = "CV-MATCH-NON-EXHAUSTIVE",
            .primary_text = {},
        },
        {
            .name = "pattern binding mismatch",
            .source = "enum Value { Integer(i32), Flag(bool) } "
                      "fn invalid(value: Value) -> i32 { return match value { "
                      ".Integer(item) | .Flag(_) => 1, _ => 0, }; }",
            .code = "CV-MATCH-BINDING-MISMATCH",
            .primary_text = {},
        },
        {
            .name = "duplicate pattern binding",
            .source = "enum Value { Pair(i32, i32) } "
                      "fn invalid(value: Value) -> i32 { return match value { "
                      ".Pair(item, item) => item, }; }",
            .code = "CV-NAME-DUPLICATE-LOCAL",
            .primary_text = {},
        },
        {
            .name = "subsumed or-pattern alternative",
            .source = "enum Value { A, B } "
                      "fn invalid(value: Value) { match value { .A | _ => {}, } }",
            .code = "CV-MATCH-DUPLICATE-ALTERNATIVE",
            .primary_text = ".A",
        },
        {
            .name = "duplicate catch alternative",
            .source = "struct Failure {} "
                      "fn fail() -> i32 throw Failure { throw Failure {}; } "
                      "fn invalid() -> i32 { return try { fail()? } catch { "
                      "Failure(_) | Failure(_) => 0, }; }",
            .code = "CV-MATCH-DUPLICATE-ALTERNATIVE",
            .primary_text = "Failure(_)",
        },
        {
            .name = "recursive value storage",
            .source = "enum Recursive { Next(Recursive), End }",
            .code = "CV-TYPE-RECURSIVE-STORAGE",
            .primary_text = {},
        },
        {
            .name = "published surface visibility leak",
            .source = "struct Hidden {} export enum Public { Value(Hidden), Empty }",
            .code = "CV-TYPE-VISIBILITY-LEAK",
            .primary_text = {},
        },
        {
            .name = "module-domain surface visibility leak",
            .source = "private struct Hidden {} fn shared() -> Hidden { return Hidden {}; }",
            .code = "CV-TYPE-VISIBILITY-LEAK",
            .primary_text = {},
        },
        {
            .name = "nested callable surface visibility leak",
            .source = "private struct Hidden {} "
                      "export fn shared(callback: fn(Hidden) -> i32) {}",
            .code = "CV-TYPE-VISIBILITY-LEAK",
            .primary_text = {},
        },
        {
            .name = "failure surface visibility leak",
            .source = "private struct Hidden {} export fn shared() throw Hidden {}",
            .code = "CV-TYPE-VISIBILITY-LEAK",
            .primary_text = {},
        },
        {
            .name = "unmarked inferred failure",
            .source = "struct Failure {} "
                      "private fn caller() -> i32 { return failing(); } "
                      "private fn failing() -> i32 { throw Failure {}; }",
            .code = "CV-EFFECT-UNMARKED",
            .primary_text = {},
        },
        {
            .name = "published callable requires a declared failure contract",
            .source = "struct Failure {} fn failing() { throw Failure {}; }",
            .code = "CV-EFFECT-THROW-PUBLISHED",
            .primary_text = {},
        },
        {
            .name = "declared failure contract does not expand",
            .source = "struct First {} struct Second {} "
                      "fn bounded() throw First { throw Second {}; }",
            .code = "CV-EFFECT-SIGNATURE-BOUND",
            .primary_text = {},
        },
        {
            .name = "partial catch",
            .source = "enum Failure { First(i32), Second(i32) } "
                      "fn fail() -> i32 throw Failure { return 0; } "
                      "fn invalid() -> i32 { return try { fail()? } catch { "
                      "Failure(.First(value)) => value, }; }",
            .code = "CV-EFFECT-CATCH-NON-EXHAUSTIVE",
            .primary_text = {},
        },
        {
            .name = "escaping capturing closure",
            .source = "fn invalid() { let value = 1; "
                      "let callback: fn(i32) -> i32 = "
                      "[value](input: i32) { return input + value; }; }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "stored callable view",
            .source = "struct Invalid { callback: fn(i32) -> i32 }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "returned callable view",
            .source = "fn invalid(callback: fn(i32) -> i32) -> fn(i32) -> i32 { "
                      "return callback; }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "inferred lambda return join cannot escape as a callable view",
            .source = "fn first(value: i32) -> i32 { return value; } "
                      "fn second(value: i32) -> i32 { return value + 1; } "
                      "fn invalid() { let choose = [](flag: bool) { "
                      "if flag { return first; } return second; }; }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "taken stored view cannot be failure-widened",
            .source = "struct First {} struct Second {} "
                      "fn narrow(value: i32) -> i32 throw First { return value; } "
                      "fn invalid() { "
                      "let source: fn(i32) -> i32 throw First = narrow; "
                      "let widened: fn(i32) -> i32 throw First + Second = &&source; }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "Write-borrow callable storage rejects a capturing target",
            .source = "fn invalid(&destination: fn(i32) -> i32) { "
                      "let offset = 1; "
                      "let owner = [offset](value: i32) { return value + offset; }; "
                      "destination = owner; }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "captured callable view",
            .source = "fn invalid(callback: fn(i32) -> i32) { "
                      "let wrapper = [callback](value: i32) { return callback(value); }; "
                      "let result = wrapper(1); }",
            .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
            .primary_text = {},
        },
        {
            .name = "Take conflicts with an active callable-view loan",
            .source = "fn invalid() { let source = 1; "
                      "let owner = [source](value: i32) { return value + source; }; "
                      "let view: fn(i32) -> i32 = owner; let moved = &&owner; "
                      "let result = view(1); }",
            .code = "CV-ACCESS-BORROW-CONFLICT",
            .primary_text = "&&",
        },
    });

    for (const auto& expectation : cases) {
        CAPTURE(expectation.name);
        auto sources = SourceManager();
        const auto source_id =
            *sources.append_virtual("diagnostic.cv", std::string(expectation.source));
        const auto input = CompilationModuleInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("diagnostic"),
        };

        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:failures").value(),
            }
        );

        CHECK(!result.has_value());
        if (result.has_value()) {
            continue;
        }
        const auto* diagnostic = find_compiler_diagnostic(result.error(), expectation.code);
        CHECK(diagnostic != nullptr);
        if (diagnostic == nullptr) {
            continue;
        }
        CHECK(diagnostic->attachment.primary.has_value());
        if (!diagnostic->attachment.primary.has_value()) {
            continue;
        }
        CHECK(!diagnostic->attachment.primary->span.span.empty());
        if (!expectation.primary_text.empty()) {
            CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), expectation.primary_text);
        }
    }
}

TEST_CASE("Compiler diagnostics: entry failure contracts are explicit regardless of visibility") {
    struct Case final {
        std::string_view source;
        std::string_view error;
    };

    const auto cases = std::array {
        Case {"fn main() {}", ""},
        Case {"fn main() -> i32 { return 7; }", ""},
        Case {"struct E {} fn main() throw E { throw E {}; }", ""},
        Case {"struct E {} private fn main() throw E {}", ""},
        Case {"struct E {} private fn main() { throw E {}; }", "CV-EFFECT-THROW-PUBLISHED"},
        Case {"struct E {} fn main() { throw E {}; }", "CV-EFFECT-THROW-PUBLISHED"},
        Case {
            "struct E {} struct F {} fn main() throw E { throw F {}; }",
            "CV-EFFECT-SIGNATURE-BOUND"
        },
        Case {"struct E {} test \"root\" { throw E {}; }", "CV-EFFECT-ROOT-UNHANDLED"},
    };
    for (const auto& item : cases) {
        CAPTURE(item.source);
        auto sources = SourceManager();
        const auto source = *sources.append_virtual("entry.cv", std::string(item.source));
        const auto input = CompilationModuleInput {
            .source_id = source,
            .module_path = *CanonicalModulePath::from_value("entry"),
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = *LinkageDomain::explicit_value("test:entry"),
            }
        );
        if (item.error.empty()) {
            CHECK(result.has_value());
        } else {
            REQUIRE_FALSE(result.has_value());
            REQUIRE_EQ(result.error().size(), 1);
            CHECK_EQ(result.error().front().finding.code, item.error);
        }
    }
}

TEST_CASE("Compiler diagnostics: failure explanations describe resolved source contracts") {
    struct Case final {
        std::string_view source;
        std::string_view code;
        std::string_view message;
        std::vector<std::string> notes;
    };

    const auto cases = std::array {
        Case {
            "fn f() -> i32 { return 1?; }",
            "CV-EFFECT-PROPAGATE-REDUNDANT",
            "'?' requires a fallible expression",
            {}
        },
        Case {
            "fn plain() -> i32 { return 1; } fn f() -> i32 { return plain()?; }",
            "CV-EFFECT-PROPAGATE-REDUNDANT",
            "'?' requires a fallible expression",
            {}
        },
        Case {
            "struct Z {} struct A {} fn f() throw Z + A {} "
            "fn main() { try { f()?; } catch {} }",
            "CV-EFFECT-CATCH-NON-EXHAUSTIVE",
            "catch does not cover every protected failure",
            {"failure type not fully covered: app.A", "failure type not fully covered: app.Z"}
        },
        Case {
            "struct A {} struct B {} fn f() throw A + B {} "
            "fn main() { try { f()?; } catch { A(_) => {} } }",
            "CV-EFFECT-CATCH-NON-EXHAUSTIVE",
            "catch does not cover every protected failure",
            {"failure type not fully covered: app.B"}
        },
        Case {
            "enum E { A, B } fn f() throw E {} "
            "fn main() { try { f()?; } catch { E(.A) => {} } }",
            "CV-EFFECT-CATCH-NON-EXHAUSTIVE",
            "catch does not cover every protected failure",
            {"failure type not fully covered: app.E"}
        },
        Case {
            "struct E {} fn f() throw E {} fn guard() -> bool { return false; } "
            "fn main() { try { f()?; } catch { E(_) if guard() => {} } }",
            "CV-EFFECT-CATCH-NON-EXHAUSTIVE",
            "catch does not cover every protected failure",
            {"failure type not fully covered: app.E"}
        },
    };
    for (const auto& item : cases) {
        CAPTURE(item.source);
        auto sources = SourceManager();
        const auto source = *sources.append_virtual("app.cv", std::string(item.source));
        const auto input = CompilationModuleInput {
            .source_id = source,
            .module_path = *CanonicalModulePath::from_value("app"),
        };
        const auto result = compile(
            sources,
            CompilationRequest {.modules = std::span(&input, 1)},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = *LinkageDomain::explicit_value("test:failure-explanations"),
            }
        );
        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_compiler_diagnostic(result.error(), item.code);
        REQUIRE(diagnostic != nullptr);
        CHECK_EQ(diagnostic->finding.message, item.message);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(diagnostic->attachment.primary->span.source_id, source);
        auto notes = std::vector<std::string>();
        for (const auto& note : diagnostic->attachment.notes) {
            notes.push_back(note.message);
        }
        CHECK_EQ(notes, item.notes);
    }
}

TEST_CASE("Compiler diagnostics: catch type names distinguish modules in stable order") {
    for (const auto reverse : {false, true}) {
        auto sources = SourceManager();
        const auto alpha =
            *sources.append_virtual("alpha.cv", "export struct E {} export fn first() throw E {}");
        const auto zeta =
            *sources.append_virtual("zeta.cv", "export struct E {} export fn second() throw E {}");
        const auto app = *sources.append_virtual(
            "app.cv",
            "import alpha using first; import zeta using second; "
            "fn main() { try { second()?; first()?; } catch {} }"
        );
        auto inputs = std::array {
            CompilationModuleInput {
                .source_id = zeta,
                .module_path = *CanonicalModulePath::from_value("zeta")
            },
            CompilationModuleInput {
                .source_id = alpha,
                .module_path = *CanonicalModulePath::from_value("alpha")
            },
            CompilationModuleInput {
                .source_id = app,
                .module_path = *CanonicalModulePath::from_value("app")
            },
        };
        if (reverse) {
            std::ranges::reverse(inputs);
        }
        const auto result = compile(
            sources,
            CompilationRequest {.modules = inputs},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = *LinkageDomain::explicit_value("test:catch-type-names"),
            }
        );
        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic =
            find_compiler_diagnostic(result.error(), "CV-EFFECT-CATCH-NON-EXHAUSTIVE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE_EQ(diagnostic->attachment.notes.size(), 2);
        CHECK_EQ(
            diagnostic->attachment.notes[0].message,
            "failure type not fully covered: alpha.E"
        );
        CHECK_EQ(diagnostic->attachment.notes[1].message, "failure type not fully covered: zeta.E");
    }
}
