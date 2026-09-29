module carven:test.internal.compiler.diagnostics.types;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler diagnostics: types failures preserve code and precise span",
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {
                    .name = "unresolved type",
                    .source = "fn invalid(value: MissingType) {}",
                    .code = DiagnosticCode::TypeUnresolved,
                    .primary_text = "MissingType",
                },
                {
                    .name = "numeric enum requires an integer representation",
                    .source = "enum Invalid: f64 { Value }",
                    .code = DiagnosticCode::TypeEnumUnderlying,
                    .primary_text = "f64",
                },
                {
                    .name = "numeric enum rejects a callable representation",
                    .source = "enum Invalid: fn() -> i32 { Value }",
                    .code = DiagnosticCode::TypeEnumUnderlying,
                    .primary_text = "fn() -> i32",
                },
                {
                    .name = "void function parameter",
                    .source = "fn invalid(value: void) {}",
                    .code = DiagnosticCode::TypeValueRequired,
                    .primary_text = "void",
                },
                {
                    .name = "void structure field",
                    .source = "struct Invalid { value: void }",
                    .code = DiagnosticCode::TypeValueRequired,
                    .primary_text = "void",
                },
                {
                    .name = "void array element",
                    .source = "fn invalid() { let values: [void; 1] = []; }",
                    .code = DiagnosticCode::TypeValueRequired,
                    .primary_text = "void",
                },
                {
                    .name = "void binding",
                    .source = "fn nothing() {} fn invalid() { let value = nothing(); }",
                    .code = DiagnosticCode::TypeValueRequired,
                    .primary_text = "nothing()",
                },
                {
                    .name = "void match subject",
                    .source = "fn nothing() {} fn invalid() { match nothing() { _ => {} } }",
                    .code = DiagnosticCode::TypeValueRequired,
                    .primary_text = "nothing()",
                },
                {
                    .name = "condition type",
                    .source = "fn invalid(value: i32) { if value {} }",
                    .code = DiagnosticCode::TypeConditionBool,
                    .primary_text = "value",
                },
                {
                    .name = "prefix operand domain",
                    .source = "fn invalid() { let value = !1; }",
                    .code = DiagnosticCode::TypePrefixBool,
                    .primary_text = "!",
                },
                {
                    .name = "binary operand domain",
                    .source = "fn invalid() { let value = true + false; }",
                    .code = DiagnosticCode::TypeBinaryNumeric,
                    .primary_text = "+",
                },
                {
                    .name = "text iteration views have no structural equality",
                    .source = "fn invalid() { let value = \"a\".bytes == \"a\".bytes; }",
                    .code = DiagnosticCode::TypeEqualityUnsupported,
                    .primary_text = "==",
                },
                {
                    .name = "text method requires a call",
                    .source = "fn invalid() { let value = \"a\".len; }",
                    .code = DiagnosticCode::TypeTextProperty,
                    .primary_text = "len",
                },
                {
                    .name = "integer literal range",
                    .source = "fn invalid() { let value = 256u8; }",
                    .code = DiagnosticCode::ConstLiteralRange,
                    .primary_text = "256u8",
                },
                {
                    .name = "negative integer literal range",
                    .source = "fn invalid() { let value = -129i8; }",
                    .code = DiagnosticCode::ConstLiteralRange,
                    .primary_text = "129i8",
                },
                {
                    .name = "constant division",
                    .source = "fn invalid() { const value = 1 / 0; }",
                    .code = DiagnosticCode::ConstDivideByZero,
                    .primary_text = "/",
                },
                {
                    .name = "constant shift",
                    .source = "fn invalid() { const value = 1u8 << 8u8; }",
                    .code = DiagnosticCode::ConstShiftRange,
                    .primary_text = "<<",
                },
                {
                    .name = "constant array index out of bounds",
                    .source = "fn invalid(values: [i32; 2]) -> i32 { return values[2]; }",
                    .code = DiagnosticCode::ConstIndexBounds,
                    .primary_text = "2",
                },
                {
                    .name = "call arity",
                    .source = "fn value(input: i32) {} fn invalid() { value(); }",
                    .code = DiagnosticCode::TypeCallArity,
                    .primary_text = "value()",
                },
                {
                    .name = "unknown construction field",
                    .source = "struct Record { value: i32 } "
                              "fn invalid() -> Record { return Record { missing: 1 }; }",
                    .code = DiagnosticCode::TypeConstructUnknownField,
                    .primary_text = "missing",
                },
                {
                    .name = "construction field type",
                    .source = "struct Record { value: i32 } "
                              "fn invalid() -> Record { return Record { value: true }; }",
                    .code = DiagnosticCode::TypeMismatch,
                    .primary_text = "true",
                },
                {
                    .name = "non-structure construction",
                    .source = "fn invalid() { let value = i32 { 1 }; }",
                    .code = DiagnosticCode::TypeConstructNotStruct,
                    .primary_text = "i32",
                },
                {
                    .name = "unresolved structure member",
                    .source = "struct Record { value: i32 } "
                              "fn invalid(record: Record) { let value = record.missing; }",
                    .code = DiagnosticCode::TypeMemberUnresolved,
                    .primary_text = "missing",
                },
                {
                    .name = "unresolved enum case",
                    .source = "enum Choice { Value } "
                              "fn invalid() { let value = Choice::Missing; }",
                    .code = DiagnosticCode::TypeMemberUnresolved,
                    .primary_text = "Missing",
                },
                {
                    .name = "unresolved contextual enum case",
                    .source = "enum Choice { Value } "
                              "fn invalid() { let value: Choice = .Missing; }",
                    .code = DiagnosticCode::TypeMemberUnresolved,
                    .primary_text = "Missing",
                },
                {
                    .name = "integer to char requires validation",
                    .source = "fn invalid(value: u32) { let character = value as char; }",
                    .code = DiagnosticCode::TypeCast,
                    .primary_text = "as",
                },
                {
                    .name = "char does not cast directly to other integer types",
                    .source = "fn invalid(value: char) { let number = value as u8; }",
                    .code = DiagnosticCode::TypeCast,
                    .primary_text = "as",
                },
                {
                    .name = "invalid cast",
                    .source = "fn invalid() { let value = true as str; }",
                    .code = DiagnosticCode::TypeCast,
                    .primary_text = "as",
                },
                {
                    .name = "unresolved contextual enum pattern",
                    .source = "enum Choice { Value } "
                              "fn invalid(input: Choice) { match input { .Missing => {} } }",
                    .code = DiagnosticCode::TypeMatchPattern,
                    .primary_text = "Missing",
                },
                {
                    .name = "positional construction missing field",
                    .source = "struct Record { first: i32, second: i32 } "
                              "fn invalid() -> Record { return Record { 1 }; }",
                    .code = DiagnosticCode::TypeConstructArity,
                    .primary_text = "1",
                },
                {
                    .name = "named construction missing field",
                    .source = "struct Record { first: i32, second: i32 } "
                              "fn invalid() -> Record { return Record { first: 1 }; }",
                    .code = DiagnosticCode::TypeConstructArity,
                    .primary_text = "first: 1",
                },
                {
                    .name = "empty literal mismatches nonzero expected array",
                    .source = "fn invalid() { let values: [i32; 1] = []; }",
                    .code = DiagnosticCode::TypeMismatch,
                    .primary_text = "[]",
                },
                {
                    .name = "throw operand type",
                    .source = "fn invalid() { throw 1; }",
                    .code = DiagnosticCode::EffectThrowType,
                    .primary_text = "throw 1;",
                },
                {
                    .name = "catch discriminator type",
                    .source = "struct Failure {} "
                              "private fn fail() -> i32 throw Failure { throw Failure {}; } "
                              "fn invalid() -> i32 { return try { fail()? } catch { "
                              "i32(_) => 0, Failure(_) => 1, }; }",
                    .code = DiagnosticCode::EffectThrowType,
                    .primary_text = "i32(_)",
                },
                {
                    .name = "assert missing condition",
                    .source = "fn invalid() { assert(); }",
                    .code = DiagnosticCode::TypeCallArity,
                    .primary_text = "assert()",
                },
                {
                    .name = "assert condition type",
                    .source = "fn invalid() { assert(1); }",
                    .code = DiagnosticCode::TypeConditionBool,
                    .primary_text = "1",
                },
                {
                    .name = "assert message checked even on success",
                    .source = "fn invalid() { assert(true, 1); }",
                    .code = DiagnosticCode::TypeMismatch,
                    .primary_text = "1",
                },
                {
                    .name = "inline-test missing condition",
                    .source = "test \"invalid\" { check(); }",
                    .code = DiagnosticCode::TestArgumentCount,
                    .primary_text = "check()",
                },
                {
                    .name = "inline-test too many arguments",
                    .source = "test \"invalid\" { require(true, \"message\", \"extra\"); }",
                    .code = DiagnosticCode::TestArgumentCount,
                    .primary_text = "require(true, \"message\", \"extra\")",
                },
                {
                    .name = "inline-test check too many arguments",
                    .source = "test \"invalid\" { check(true, \"message\", \"extra\"); }",
                    .code = DiagnosticCode::TestArgumentCount,
                    .primary_text = "check(true, \"message\", \"extra\")",
                },
                {
                    .name = "inline-test require missing condition",
                    .source = "test \"invalid\" { require(); }",
                    .code = DiagnosticCode::TestArgumentCount,
                    .primary_text = "require()",
                },
                {
                    .name = "inline-test fail arity",
                    .source = "test \"invalid\" { fail(\"first\", \"second\"); }",
                    .code = DiagnosticCode::TestArgumentCount,
                    .primary_text = "fail(\"first\", \"second\")",
                },
                {
                    .name = "inline-test condition type",
                    .source = "test \"invalid\" { check(1); }",
                    .code = DiagnosticCode::TestConditionType,
                    .primary_text = "1",
                },
                {
                    .name = "inline-test message type",
                    .source = "test \"invalid\" { require(true, 1); }",
                    .code = DiagnosticCode::TestMessageType,
                    .primary_text = "1",
                },
                {
                    .name = "inline-test check message type",
                    .source = "test \"invalid\" { check(true, 1); }",
                    .code = DiagnosticCode::TestMessageType,
                    .primary_text = "1",
                },
                {
                    .name = "inline-test fail message type",
                    .source = "test \"invalid\" { fail(1); }",
                    .code = DiagnosticCode::TestMessageType,
                    .primary_text = "1",
                },
                {
                    .name = "check uses ordinary Read argument access",
                    .source = "fn invalid() { var value = true; check(&value); }",
                    .code = DiagnosticCode::AccessCallMismatch,
                    .primary_text = "&value",
                },
                {
                    .name = "print requires a value",
                    .source = "fn invalid() { print(); }",
                    .code = DiagnosticCode::TypeCallArity,
                    .primary_text = "print()",
                },
                {
                    .name = "builtin callable requires a concrete signature",
                    .source = "fn invalid() { let output = println; }",
                    .code = DiagnosticCode::TypeNotCallable,
                    .primary_text = "println",
                },
                {
                    .name = "std testing is an ordinary missing module",
                    .source = "import std::testing using check; fn invalid() {}",
                    .code = DiagnosticCode::ImportResolution,
                    .primary_text = "std::testing",
                },
            });
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Compiler diagnostics: expression body result contracts preserve source locations",
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "direct result cycle",
                 .source = "fn recurse() => recurse();",
                 .code = DiagnosticCode::TypeResultInferenceCycle,
                 .primary_text = "recurse"},
                {.name = "mutual result cycle",
                 .source = "fn first() => second(); fn second() => first();",
                 .code = DiagnosticCode::TypeResultInferenceCycle,
                 .primary_text = "first"},
                {.name = "explicit void expression body",
                 .source = "fn wrong() -> void => 1;",
                 .code = DiagnosticCode::TypeReturnValue,
                 .primary_text = "=> 1"},
                {.name = "lambda parameters still require context",
                 .source = "fn wrong() { let f = [](a) => a; }",
                 .code = DiagnosticCode::LambdaSignatureInference,
                 .primary_text = "a"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Compiler diagnostics: callable result inference is independent of body syntax",
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "native return widths require a declared common result",
                 .source =
                     "import <cstdint>; fn select(flag: bool, a: ::std::int32_t, b: ::std::int64_t) { if flag { return a; } return b; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "b"},
                {.name = "reversing native returns does not select a different result",
                 .source =
                     "import <cstdint>; fn select(flag: bool, a: ::std::int32_t, b: ::std::int64_t) { if flag { return b; } return a; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "a"},
                {.name = "block result dependency cycle",
                 .source = "fn first() { return second(); } fn second() { return first(); }",
                 .code = DiagnosticCode::TypeResultInferenceCycle,
                 .primary_text = "first"},
                {.name = "inferred value result requires complete returns",
                 .source = "fn partial(flag: bool) { if flag { return 1; } }",
                 .code = DiagnosticCode::FlowMissingReturn,
                 .primary_text = "{ if flag { return 1; } }"},
                {.name = "earlier return does not contextually convert later literal",
                 .source = "fn inconsistent(flag: bool) { if flag { return 1u8; } return 2; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "2"},
                {.name = "reversing returns preserves the type conflict",
                 .source = "fn inconsistent(flag: bool) { if flag { return 2; } return 1u8; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "1u8"},
                {.name = "lambda follows the same independent return rule",
                 .source =
                     "fn outer() { let f = [](flag: bool) { if flag { return 1u8; } return 2; }; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "2"},
                {.name = "bare return conflicts with inferred value",
                 .source = "fn inconsistent(flag: bool) { if flag { return 1; } return; }",
                 .code = DiagnosticCode::TypeMissingReturnValue,
                 .primary_text = "return;"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test("Compiler diagnostics: structure equality is unavailable across source contexts", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "scalar fields do not imply equality",
             .source =
                 "struct Record { value: i32 } fn compare(a: Record, b: Record) -> bool => a == b;",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "const function bodies obey the same operand rules",
             .source =
                 "struct Record { value: i32 } const fn compare(a: Record, b: Record) -> bool => a != b;",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "!="},
            {.name = "array equality requires element equality",
             .source =
                 "struct Record { value: i32 } fn compare(a: [Record; 1], b: [Record; 1]) -> bool => a == b;",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "zero extent does not waive element equality",
             .source =
                 "struct Record { value: i32 } fn compare(a: [Record; 0], b: [Record; 0]) -> bool => a == b;",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "enum equality requires every payload to support equality",
             .source =
                 "struct Record { value: i32 } enum Payload { Value(Record), Empty } const equal = Payload::Empty == Payload::Empty;",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "empty structure",
             .source = "struct Empty {} fn compare(a: Empty, b: Empty) -> bool => a == b;",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "required constant equality",
             .source = "struct Record { value: i32 } const equal = Record { 1 } == Record { 1 };",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "constant block inequality",
             .source =
                 "struct Record { value: i32 } const { let equal = Record { 1 } != Record { 1 }; }",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "!="},
        });
        check_compiler_errors(cases);
    });
});

} // namespace
