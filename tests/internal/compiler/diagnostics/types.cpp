module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.types;

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

TEST_CASE("Compiler diagnostics: types failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {
            .name = "unresolved type",
            .source = "fn invalid(value: MissingType) {}",
            .code = "CV-TYPE-UNRESOLVED",
            .primary_text = "MissingType",
        },
        {
            .name = "numeric enum requires an integer representation",
            .source = "enum Invalid: f64 { Value }",
            .code = "CV-TYPE-ENUM-UNDERLYING",
            .primary_text = "f64",
        },
        {
            .name = "numeric enum rejects a callable representation",
            .source = "enum Invalid: fn() -> i32 { Value }",
            .code = "CV-TYPE-ENUM-UNDERLYING",
            .primary_text = "fn() -> i32",
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
            .name = "text method requires a call",
            .source = "fn invalid() { let value = \"a\".len; }",
            .code = "CV-TYPE-TEXT-PROPERTY",
            .primary_text = "len",
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
            .name = "constant array index out of bounds",
            .source = "fn invalid(values: [i32; 2]) -> i32 { return values[2]; }",
            .code = "CV-CONST-INDEX-BOUNDS",
            .primary_text = "2",
        },
        {
            .name = "call arity",
            .source = "fn value(input: i32) {} fn invalid() { value(); }",
            .code = "CV-TYPE-CALL-ARITY",
            .primary_text = "value()",
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
            .name = "catch discriminator type",
            .source = "struct Failure {} "
                      "private fn fail() -> i32 throw Failure { throw Failure {}; } "
                      "fn invalid() -> i32 { return try { fail()? } catch { "
                      "i32(_) => 0, Failure(_) => 1, }; }",
            .code = "CV-EFFECT-THROW-TYPE",
            .primary_text = "i32(_)",
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
            .name = "std testing is an ordinary missing module",
            .source = "import std::testing using check; fn invalid() {}",
            .code = "CV-IMPORT-RESOLUTION",
            .primary_text = "std::testing",
        },
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: expression body result contracts preserve source locations") {
    constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "direct result cycle",
         .source = "fn recurse() => recurse();",
         .code = "CV-TYPE-RESULT-INFERENCE-CYCLE",
         .primary_text = "recurse"},
        {.name = "mutual result cycle",
         .source = "fn first() => second(); fn second() => first();",
         .code = "CV-TYPE-RESULT-INFERENCE-CYCLE",
         .primary_text = "first"},
        {.name = "explicit void expression body",
         .source = "fn wrong() -> void => 1;",
         .code = "CV-TYPE-RETURN-VALUE",
         .primary_text = "=> 1"},
        {.name = "lambda parameters still require context",
         .source = "fn wrong() { let f = [](a) => a; }",
         .code = "CV-LAMBDA-SIGNATURE-INFERENCE",
         .primary_text = "a"},
    });
    check_compiler_errors(cases);
}
