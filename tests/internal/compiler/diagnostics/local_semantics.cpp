module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.local_semantics;

import :artifacts;
import :compilation.request;
import :compiler.compile;
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

TEST_CASE("Compiler diagnostics: local semantic failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<ErrorExpectation>({
        {
            .name = "unresolved value",
            .source = "fn invalid() { missing(); }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "missing",
        },
        {
            .name = "unresolved type",
            .source = "fn invalid(value: MissingType) {}",
            .code = "CV-TYPE-UNRESOLVED",
            .primary_text = "MissingType",
        },
        {
            .name = "void function parameter",
            .source = "fn invalid(value: void) {}",
            .code = "CV-TYPE-VALUE-REQUIRED",
            .primary_text = "void",
        },
        {
            .name = "void structure field",
            .source = "struct Invalid { value: void }",
            .code = "CV-TYPE-VALUE-REQUIRED",
            .primary_text = "void",
        },
        {
            .name = "void array element",
            .source = "fn invalid() { let values: [void; 1] = []; }",
            .code = "CV-TYPE-VALUE-REQUIRED",
            .primary_text = "void",
        },
        {
            .name = "void binding",
            .source = "fn nothing() {} fn invalid() { let value = nothing(); }",
            .code = "CV-TYPE-VALUE-REQUIRED",
            .primary_text = "nothing()",
        },
        {
            .name = "void match subject",
            .source = "fn nothing() {} fn invalid() { match nothing() { _ => {} } }",
            .code = "CV-TYPE-VALUE-REQUIRED",
            .primary_text = "nothing()",
        },
        {
            .name = "binding is not visible in its own initializer",
            .source = "fn invalid() { let value: i32 = value; }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "value",
        },
        {
            .name = "duplicate parameter",
            .source = "fn invalid(value: i32, value: i32) {}",
            .code = "CV-NAME-DUPLICATE-PARAMETER",
            .primary_text = "value",
        },
        {
            .name = "condition type",
            .source = "fn invalid(value: i32) { if value {} }",
            .code = "CV-TYPE-CONDITION-BOOL",
            .primary_text = "value",
        },
        {
            .name = "prefix operand domain",
            .source = "fn invalid() { let value = !1; }",
            .code = "CV-TYPE-PREFIX-BOOL",
            .primary_text = "!",
        },
        {
            .name = "binary operand domain",
            .source = "fn invalid() { let value = true + false; }",
            .code = "CV-TYPE-BINARY-NUMERIC",
            .primary_text = "+",
        },
        {
            .name = "text iteration views have no structural equality",
            .source = "fn invalid() { let value = \"a\".bytes == \"a\".bytes; }",
            .code = "CV-TYPE-EQUALITY-UNSUPPORTED",
            .primary_text = "==",
        },
        {
            .name = "integer literal range",
            .source = "fn invalid() { let value = 256u8; }",
            .code = "CV-CONST-LITERAL-RANGE",
            .primary_text = "256u8",
        },
        {
            .name = "negative integer literal range",
            .source = "fn invalid() { let value = -129i8; }",
            .code = "CV-CONST-LITERAL-RANGE",
            .primary_text = "129i8",
        },
        {
            .name = "constant division",
            .source = "fn invalid() { const value = 1 / 0; }",
            .code = "CV-CONST-DIVIDE-BY-ZERO",
            .primary_text = "/",
        },
        {
            .name = "constant shift",
            .source = "fn invalid() { const value = 1u8 << 8u8; }",
            .code = "CV-CONST-SHIFT-RANGE",
            .primary_text = "<<",
        },
        {
            .name = "call arity",
            .source = "fn value(input: i32) {} fn invalid() { value(); }",
            .code = "CV-TYPE-CALL-ARITY",
            .primary_text = "value()",
        },
        {
            .name = "immutable assignment",
            .source = "fn invalid() { let value = 1; value = 2; }",
            .code = "CV-ACCESS-IMMUTABLE",
            .primary_text = "value",
        },
        {
            .name = "unknown construction field",
            .source = "struct Record { value: i32 } "
                      "fn invalid() -> Record { return Record { missing: 1 }; }",
            .code = "CV-TYPE-CONSTRUCT-UNKNOWN-FIELD",
            .primary_text = "missing",
        },
        {
            .name = "construction field type",
            .source = "struct Record { value: i32 } "
                      "fn invalid() -> Record { return Record { value: true }; }",
            .code = "CV-TYPE-CONSTRUCT-FIELD",
            .primary_text = "true",
        },
        {
            .name = "non-structure construction",
            .source = "fn invalid() { let value = i32 {}; }",
            .code = "CV-TYPE-CONSTRUCT-NOT-STRUCT",
            .primary_text = "i32",
        },
        {
            .name = "unresolved structure member",
            .source = "struct Record { value: i32 } "
                      "fn invalid(record: Record) { let value = record.missing; }",
            .code = "CV-TYPE-MEMBER-UNRESOLVED",
            .primary_text = "missing",
        },
        {
            .name = "unresolved enum case",
            .source = "enum Choice { Value } "
                      "fn invalid() { let value = Choice::Missing; }",
            .code = "CV-TYPE-MEMBER-UNRESOLVED",
            .primary_text = "Missing",
        },
        {
            .name = "unresolved contextual enum case",
            .source = "enum Choice { Value } "
                      "fn invalid() { let value: Choice = .Missing; }",
            .code = "CV-TYPE-MEMBER-UNRESOLVED",
            .primary_text = "Missing",
        },
        {
            .name = "invalid cast",
            .source = "fn invalid() { let value = true as str; }",
            .code = "CV-TYPE-CAST",
            .primary_text = "as",
        },
        {
            .name = "unresolved contextual enum pattern",
            .source = "enum Choice { Value } "
                      "fn invalid(input: Choice) { match input { .Missing => {} } }",
            .code = "CV-TYPE-MATCH-PATTERN",
            .primary_text = "Missing",
        },
        {
            .name = "positional construction missing field",
            .source = "struct Record { first: i32, second: i32 } "
                      "fn invalid() -> Record { return Record { 1 }; }",
            .code = "CV-TYPE-CONSTRUCT-ARITY",
            .primary_text = "1",
        },
        {
            .name = "named construction missing field",
            .source = "struct Record { first: i32, second: i32 } "
                      "fn invalid() -> Record { return Record { first: 1 }; }",
            .code = "CV-TYPE-CONSTRUCT-ARITY",
            .primary_text = "first: 1",
        },
        {
            .name = "empty literal mismatches nonzero expected array",
            .source = "fn invalid() { let values: [i32; 1] = []; }",
            .code = "CV-TYPE-MISMATCH",
            .primary_text = "[]",
        },
        {
            .name = "throw operand type",
            .source = "fn invalid() { throw 1; }",
            .code = "CV-EFFECT-THROW-TYPE",
            .primary_text = "throw 1;",
        },
        {
            .name = "inline-test missing condition",
            .source = "test \"invalid\" { check(); }",
            .code = "CV-TEST-ARGUMENT-COUNT",
            .primary_text = "check();",
        },
        {
            .name = "inline-test too many arguments",
            .source = "test \"invalid\" { require(true, \"message\", \"extra\"); }",
            .code = "CV-TEST-ARGUMENT-COUNT",
            .primary_text = "require(true, \"message\", \"extra\");",
        },
        {
            .name = "inline-test check too many arguments",
            .source = "test \"invalid\" { check(true, \"message\", \"extra\"); }",
            .code = "CV-TEST-ARGUMENT-COUNT",
            .primary_text = "check(true, \"message\", \"extra\");",
        },
        {
            .name = "inline-test require missing condition",
            .source = "test \"invalid\" { require(); }",
            .code = "CV-TEST-ARGUMENT-COUNT",
            .primary_text = "require();",
        },
        {
            .name = "inline-test fail arity",
            .source = "test \"invalid\" { fail(\"first\", \"second\"); }",
            .code = "CV-TEST-ARGUMENT-COUNT",
            .primary_text = "fail(\"first\", \"second\");",
        },
        {
            .name = "inline-test condition type",
            .source = "test \"invalid\" { check(1); }",
            .code = "CV-TEST-CONDITION-TYPE",
            .primary_text = "1",
        },
        {
            .name = "inline-test message type",
            .source = "test \"invalid\" { require(true, 1); }",
            .code = "CV-TEST-MESSAGE-TYPE",
            .primary_text = "1",
        },
        {
            .name = "inline-test check message type",
            .source = "test \"invalid\" { check(true, 1); }",
            .code = "CV-TEST-MESSAGE-TYPE",
            .primary_text = "1",
        },
        {
            .name = "inline-test fail message type",
            .source = "test \"invalid\" { fail(1); }",
            .code = "CV-TEST-MESSAGE-TYPE",
            .primary_text = "1",
        },
        {
            .name = "inline-test foreign condition is not bool",
            .source = "test \"invalid\" { check(#[cpp] { true }); }",
            .code = "CV-TEST-CONDITION-TYPE",
            .primary_text = "#[cpp] { true }",
        },
        {
            .name = "inline-test foreign message is not str",
            .source = "test \"invalid\" { fail(#[cpp] { \"message\" }); }",
            .code = "CV-TEST-MESSAGE-TYPE",
            .primary_text = "#[cpp] { \"message\" }",
        },
        {
            .name = "production has no implicit check name",
            .source = "fn invalid() { check(true); }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "check",
        },
        {
            .name = "lambda clears inline-test context",
            .source = "test \"invalid\" { let callback = []() { check(true); }; }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "check",
        },
        {
            .name = "std testing is an ordinary missing module",
            .source = "import std::testing using check; fn invalid() {}",
            .code = "CV-IMPORT-RESOLUTION",
            .primary_text = "std::testing",
        },
    });

    for (const auto& expectation : cases) {
        CAPTURE(expectation.name);
        auto sources = SourceManager();
        const auto source_id =
            *sources.append_virtual("diagnostic.cv", std::string(expectation.source));
        const auto input = CompilationInput {
            .source_id = source_id,
            .module_path = *CanonicalModulePath::from_value("diagnostic"),
        };

        const auto result = compile(
            sources,
            CompilationRequest {.inputs = std::span(&input, 1)},
            TargetGenerationRequest {
                .tests = TestEmissionMode::None,
                .linkage_domain = LinkageDomain::explicit_value("test:local-semantics").value(),
            }
        );

        REQUIRE(!result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), expectation.code);
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), expectation.primary_text);
    }
}

TEST_CASE("Compiler diagnostics: successful compilation retains warning location") {
    auto sources = SourceManager();
    const auto source = std::string(
        "fn value() {\n"
        "    let unused = 1;\n"
        "    return;\n"
        "    let unreachable = 1;\n"
        "}\n"
    );
    const auto source_id = *sources.append_virtual("warning.cv", source);
    const auto input = CompilationInput {
        .source_id = source_id,
        .module_path = *CanonicalModulePath::from_value("warning"),
    };

    const auto result = compile(
        sources,
        CompilationRequest {.inputs = std::span(&input, 1)},
        TargetGenerationRequest {
            .tests = TestEmissionMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:local-semantics").value(),
        }
    );

    REQUIRE(result.has_value());
    const auto* unreachable = find_diagnostic(result->diagnostics, "CV-FLOW-UNREACHABLE");
    REQUIRE(unreachable != nullptr);
    REQUIRE(unreachable->attachment.primary.has_value());
    CHECK_EQ(sources.location(unreachable->attachment.primary->span).line, 4u);
    CHECK_EQ(sources.slice(unreachable->attachment.primary->span), "let unreachable = 1;");

    const auto* unused = find_diagnostic(result->diagnostics, "CV-LINT-UNUSED-LOCAL");
    REQUIRE(unused != nullptr);
    REQUIRE(unused->attachment.primary.has_value());
    CHECK_EQ(sources.slice(unused->attachment.primary->span), "unused");
}
