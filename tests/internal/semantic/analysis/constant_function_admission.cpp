module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constant_function_admission;

import :diagnostics.code;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Const fn admission: supported definitions do not require a call site") {
    const auto sources = std::to_array<std::string_view>({
        R"(const fn empty() {})",
        R"(const fn output() { println(1); })",
        R"(const fn inactive() { if false { println(1); } })",
        R"(const fn report() { check(true); })",
        R"(struct Value { number: i32 }
            const fn field() -> i32 { let value = Value { number: 1 }; return value.number; })",
        R"(const fn integer(value: i32) -> i32 {
            return ((value + 2) * 3) ^ (~value);
        })",
        R"(const fn casts(value: i32) -> u32 {
            let present = (value != 0) as u32;
            return present + ('A' as u32);
        })",
        R"(const fn loop(limit: i32) -> i32 {
            var total = 0;
            var index = 0;
            while index < limit {
                index += 1;
                if index == 2 { continue; }
                total += index;
                if total > 100 { break; }
            }
            for var step = 0; step < limit; ++step { total += step; }
            for value in 0..limit { total += value; }
            return if total > 0 { total } else { 0 };
        })",
        R"(const fn choose(value: i32) -> i32 {
            return match value { 0 | 1 => 2, selected => selected, };
        })",
        R"(const fn text(input: str) -> String {
            let empty = String::new();
            var result = String::from_str(input);
            if result.as_str().is_empty() { result.push('!'); }
            result.append("?");
            let copy = result;
            result.clear();
            return &&copy;
        })",
        R"(const fn format(value: i32) -> String { return f"value-{value:04}"; })",
        R"(const fn consume(&&value: String) -> usize { return value.len(); })",
        R"(const fn caller(value: i32) -> i32 { return callee(value); }
           const fn callee(value: i32) -> i32 { return value + 1; })",
        R"(const fn factorial(value: i32) -> i32 {
            if value < 2 { return 1; }
            return value * factorial(value - 1);
        })",
    });
    for (const auto source : sources) {
        CAPTURE(source);
        const auto program = analyze_test_program(std::string(source));
        CHECK(program.declarations().functions().size() > 0uz);
        for (const auto entry : program.declarations().functions()) {
            CHECK(entry.value.is_const);
        }
    }
}

TEST_CASE("Const fn admission: unsupported operations are rejected in unused and inactive source") {
    struct Scenario final {
        std::string_view name;
        std::string_view source;
    };

    const auto scenarios = std::to_array<Scenario>({
        {"unused native call", R"(import "provider.hpp";
            const fn invalid() -> i32 { return ::provider::value() as i32; })"},
        {"inactive ordinary call", R"(fn ordinary() {}
            const fn invalid() { if false { ordinary(); } })"},
        {"inactive short circuit call", R"(fn ordinary() -> bool => true;
            const fn invalid() -> bool { return false && ordinary(); })"},
        {"Write parameter", R"(const fn invalid(&value: i32) { value = 1; })"},
        {"declared typed failure", R"(struct Failure {}
            const fn invalid() throw Failure {})"},
        {"throw operation", R"(struct Failure {}
            private const fn invalid() { throw Failure {}; })"},
        {"floating signature", R"(const fn invalid(value: f64) -> f64 { return value; })"},
        {"floating expression", R"(const fn invalid() { let value = 1.0 + 2.0; })"},
        {"pointer signature", R"(const fn invalid(value: ptr<i32>) -> bool {
            return value == nullptr;
        })"},
        {"slice signature", R"(const fn invalid(value: [i32]) -> usize { return value.len(); })"},
        {"array operation", R"(const fn invalid() -> usize { return [1, 2].as_slice().len(); })"},
        {"enum signature", R"(enum Value { One, Two }
            const fn invalid(value: Value) -> bool { return value == Value::One; })"},
        {"closure value", R"(const fn invalid() -> i32 { let call = []() => 1; return call(); })"},
        {"unchecked scalar construction", R"(const fn invalid() -> char {
            return char::from_u32_unchecked(65u32);
        })"},
        {"character range", R"(const fn invalid(value: str) {
            for character in value.chars { let copy = character; }
        })"},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        const auto diagnostics = analyze_test_errors(std::string(scenario.source));
        const auto* finding = find_diagnostic_code(diagnostics, DiagnosticCode::ConstAdmission);
        REQUIRE(finding != nullptr);
        CHECK(finding->attachment.primary.has_value());
    }
}
