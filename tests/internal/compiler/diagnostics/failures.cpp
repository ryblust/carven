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
import std;

namespace {

auto find_diagnostic(std::span<const Diagnostic> diagnostics, std::string_view code) noexcept
    -> const Diagnostic* {
    const auto found =
        std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        });
    return found == diagnostics.end() ? nullptr : &*found;
}

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
        const auto* warning = find_diagnostic(result->diagnostics, expectation.code);
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
            .name = "entry point handles every failure",
            .source = "struct Failure {} fn main() { throw Failure {}; }",
            .code = "CV-EFFECT-ROOT-UNHANDLED",
            .primary_text = {},
        },
        {
            .name = "test handles every failure",
            .source = "struct Failure {} test \"failure\" { throw Failure {}; }",
            .code = "CV-EFFECT-ROOT-UNHANDLED",
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
        const auto* diagnostic = find_diagnostic(result.error(), expectation.code);
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
