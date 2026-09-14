module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constant_arrays;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.text;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto named_constant(const SemIRProgram& program, std::string_view name) noexcept
    -> const ConstantFact& {
    auto found = std::optional<ConstantID>();
    for (const auto declaration : program.declarations().module_constants()) {
        if (program.provenance().spelling(declaration.value.name) == name) {
            found = declaration.value.value;
        }
    }
    REQUIRE(found.has_value());
    return program.constants().constant(*found);
}

auto integer(const ConstantFact& fact) noexcept -> std::int64_t {
    const auto* value = std::get_if<IntegerConstant>(&fact.value);
    REQUIRE(value != nullptr);
    REQUIRE(value->as_signed().has_value());
    return *value->as_signed();
}

auto elements(const ConstantFact& fact) noexcept -> std::span<const ConstantID> {
    const auto* array = std::get_if<ArrayConstant>(&fact.value);
    REQUIRE(array != nullptr);
    return array->elements;
}

} // namespace

TEST_CASE("Constant arrays: nested construction mutation copy and Take retain fixed types") {
    const auto program = analyze_test_program(R"(
        const result = build(10);
        const empty = nothing();
        const fn relay(&&value: [[i32; 2]; 2]) -> [[i32; 2]; 2] => &&value;
        const fn build(seed: i32) -> [[i32; 2]; 2] {
            var values = [[1, 2], [3, 4]];
            let copy = values;
            for row in 0..2 {
                for column in 0..2 { values[row][column] += seed; }
            }
            values = relay(&&values);
            values[0][0] += copy[0][0];
            return &&values;
        }
        const fn nothing() -> [i32; 0] => [];
    )");
    const auto& fact = named_constant(program, "result");
    const auto* outer_type = std::get_if<ArrayTypeValue>(&program.types().type(fact.type).value);
    REQUIRE(outer_type != nullptr);
    CHECK(outer_type->extent == 2u);
    const auto rows = elements(fact);
    REQUIRE(rows.size() == 2uz);
    const auto& first = elements(program.constants().constant(rows[0]));
    const auto& second = elements(program.constants().constant(rows[1]));
    REQUIRE(first.size() == 2uz);
    REQUIRE(second.size() == 2uz);
    CHECK(integer(program.constants().constant(first[0])) == 12);
    CHECK(integer(program.constants().constant(first[1])) == 12);
    CHECK(integer(program.constants().constant(second[0])) == 13);
    CHECK(integer(program.constants().constant(second[1])) == 14);
    CHECK(elements(named_constant(program, "empty")).empty());
}

TEST_CASE("Constant arrays: scalar snapshots and projected Read storage follow operand order") {
    const auto program = analyze_test_program(R"(
        const result = run();
        const fn observe(values: [i32; 2], snapshot: i32, effect: i32) -> i32 {
            return values[0] * 100 + snapshot * 10 + effect;
        }
        const fn run() -> i32 {
            var values = [[1, 2], [3, 4]];
            return observe(values[0], values[0][0], if true {
                values = [[7, 8], [9, 10]];
                2
            } else { 0 });
        }
    )");
    CHECK(integer(named_constant(program, "result")) == 712);
}

TEST_CASE("Constant arrays: assignment retains its selected element across owner replacement") {
    const auto program = analyze_test_program(R"(
        const result = run();
        const fn run() -> i32 {
            var values = [[1, 2], [3, 4]];
            var selections = 0;
            values[if true { selections += 1; 0 } else { 1 }][0] = if true {
                values = [[20, 30], [40, 50]];
                7
            } else { 0 };
            values[0][0] += if true { values = [[80, 90], [40, 50]]; 3 } else { 0 };
            return values[0][0] + values[0][1] + selections * 100;
        }
    )");
    CHECK(integer(named_constant(program, "result")) == 200);
}

TEST_CASE("Constant arrays: match selects an indexed subject once before guards") {
    const auto program = analyze_test_program(R"(
        const result = run();
        const fn run() -> i32 {
            var values = [1, 2];
            var selections = 0;
            var index = 0;
            let chosen = match values[if true { selections += 1; index } else { 1 }] {
                1 if if true { index = 1; false } else { true } => 0,
                selected => selected,
            };
            return chosen + selections * 10;
        }
    )");
    CHECK(integer(named_constant(program, "result")) == 11);
}

TEST_CASE("Constant arrays: text bool and character elements preserve their canonical types") {
    const auto program = analyze_test_program(R"(
        const text = texts();
        const boolean = booleans();
        const character = characters();
        const fn texts() -> [str; 2] => ["我", "\0😀"];
        const fn booleans() -> [bool; 2] => [true, false];
        const fn characters() -> [char; 2] => ['我', '😀'];
    )");
    const auto text = elements(named_constant(program, "text"));
    REQUIRE(text.size() == 2uz);
    const auto& text_fact = program.constants().constant(text[1]);
    CHECK(
        std::get<BuiltinTypeValue>(program.types().type(text_fact.type).value).kind
        == BuiltinType::Str
    );
    CHECK(
        program.provenance().spelling(std::get<StringConstant>(text_fact.value).value)
        == std::string_view("\0😀", 5uz)
    );
    const auto boolean = elements(named_constant(program, "boolean"));
    REQUIRE(boolean.size() == 2uz);
    CHECK_FALSE(std::get<BooleanConstant>(program.constants().constant(boolean[1]).value).value);
    const auto character = elements(named_constant(program, "character"));
    REQUIRE(character.size() == 2uz);
    CHECK(
        std::get<CharacterConstant>(program.constants().constant(character[1]).value).scalar
        == U'😀'
    );
}

TEST_CASE("Constant arrays: execution diagnoses dynamic negative and upper-bound indices") {
    for (const auto index : {-1, 2}) {
        const auto source = std::format(
            "const fn read(index: i32) -> i32 {{ let values = [1, 2]; return values[index]; }} "
            "const result = read({});",
            index
        );
        const auto diagnostics = analyze_test_errors(source);
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstIndexBounds));
    }
}

TEST_CASE("Constant arrays: equality compares nested values after independent construction") {
    const auto program = analyze_test_program(R"(
        const result = equal();
        const fn equal() -> bool {
            let first = [[1, 2], [3, 4]];
            var second = first;
            let before = first == second;
            second[1][0] = 9;
            return before && first != second && ["我", "😀"] == ["我", "😀"];
        }
    )");
    CHECK(std::get<BooleanConstant>(named_constant(program, "result").value).value);
}

TEST_CASE("Constant arrays: unsupported element types are rejected without executing a call") {
    const auto sources = std::to_array<std::string_view>({
        "const fn invalid(value: [String; 1]) -> [String; 1] => value;",
        "const fn invalid(value: [[f64; 1]; 1]) -> [[f64; 1]; 1] => value;",
        "const fn invalid(value: [ptr<i32>; 1]) -> [ptr<i32>; 1] => value;",
        "const fn invalid(value: [[i32]; 1]) -> [[i32]; 1] => value;",
    });
    for (const auto source : sources) {
        CAPTURE(source);
        const auto diagnostics = analyze_test_errors(std::string(source));
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstAdmission));
    }
}

TEST_CASE("Constant arrays: execution preserves the ownership publication gate after Take") {
    const auto diagnostics = analyze_test_errors(R"(
        const fn moved() -> i32 {
            let value = [1, 2];
            let owner = &&value;
            return value[0];
        }
        const result = moved();
    )");
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
    CHECK_FALSE(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstEvaluation));
}
