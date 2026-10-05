module carven:test.internal.semantic.analysis.constant_arrays;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
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
    require(found.has_value());
    return program.constants().constant(*found);
}

auto integer(const ConstantFact& fact) noexcept -> std::int64_t {
    const auto* value = std::get_if<IntegerConstant>(&fact.value);
    require(value != nullptr);
    require(value->as_signed().has_value());
    return *value->as_signed();
}

auto elements(const ConstantFact& fact) noexcept -> std::span<const ConstantID> {
    const auto* array = std::get_if<ArrayConstant>(&fact.value);
    require(array != nullptr);
    return array->elements;
}

const TestSuite suite([] static noexcept {
    "Constant arrays: nested construction mutation copy and Take retain fixed types"_test =
        [] static noexcept {
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
            const auto* outer_type =
                std::get_if<ArrayTypeValue>(&program.types().type(fact.type).value);
            if (!expect(outer_type != nullptr)) {
                return;
            }
            expect(outer_type->extent == 2u);
            const auto rows = elements(fact);
            if (!expect(rows.size() == 2uz)) {
                return;
            }
            const auto& first = elements(program.constants().constant(rows[0]));
            const auto& second = elements(program.constants().constant(rows[1]));
            if (!expect(first.size() == 2uz)) {
                return;
            }
            if (!expect(second.size() == 2uz)) {
                return;
            }
            expect(integer(program.constants().constant(first[0])) == 12);
            expect(integer(program.constants().constant(first[1])) == 12);
            expect(integer(program.constants().constant(second[0])) == 13);
            expect(integer(program.constants().constant(second[1])) == 14);
            expect(elements(named_constant(program, "empty")).empty());
        };

    "Constant arrays: scalar snapshots and projected Read storage follow operand order"_test =
        [] static noexcept {
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
            expect(integer(named_constant(program, "result")) == 712);
        };

    "Constant arrays: assignment retains its selected element across owner replacement"_test =
        [] static noexcept {
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
            expect(integer(named_constant(program, "result")) == 200);
        };

    "Constant arrays: match selects an indexed subject once before guards"_test =
        [] static noexcept {
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
            expect(integer(named_constant(program, "result")) == 11);
        };

    "Constant arrays: text bool and character elements preserve their canonical types"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const text = texts();
        const boolean = booleans();
        const character = characters();
        const fn texts() -> [str; 2] => ["我", "\0😀"];
        const fn booleans() -> [bool; 2] => [true, false];
        const fn characters() -> [char; 2] => ['我', '😀'];
    )");
            const auto text = elements(named_constant(program, "text"));
            if (!expect(text.size() == 2uz)) {
                return;
            }
            const auto& text_fact = program.constants().constant(text[1]);
            expect(
                std::get<BuiltinTypeValue>(program.types().type(text_fact.type).value).kind
                == BuiltinType::Str
            );
            expect(
                program.provenance().spelling(std::get<StringConstant>(text_fact.value).value)
                == std::string_view("\0😀", 5uz)
            );
            const auto boolean = elements(named_constant(program, "boolean"));
            if (!expect(boolean.size() == 2uz)) {
                return;
            }
            expect(
                !(std::get<BooleanConstant>(program.constants().constant(boolean[1]).value).value)
            );
            const auto character = elements(named_constant(program, "character"));
            if (!expect(character.size() == 2uz)) {
                return;
            }
            expect(
                std::get<CharacterConstant>(program.constants().constant(character[1]).value).scalar
                == U'😀'
            );
        };

    "Constant arrays: execution diagnoses dynamic negative and upper-bound indices"_test =
        [] static noexcept {
            for (const auto index : {-1, 2}) {
                const auto source = std::format(
                    "const fn read(index: i32) -> i32 {{ let values = [1, 2]; return values[index]; }} "
                    "const result = read({});",
                    index
                );
                const auto diagnostics = analyze_test_errors(source);
                expect_diagnostic(diagnostics, DiagnosticCode::ConstIndexBounds);
            }
        };

    "Constant arrays: equality compares nested values after independent construction"_test =
        [] static noexcept {
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
            expect(std::get<BooleanConstant>(named_constant(program, "result").value).value);
        };

    "Constant arrays: unused functions do not require executable element types"_test =
        [] static noexcept {
            const auto sources = std::to_array<std::string_view>({
                "fn invalid(value: [ptr<i32>; 1]) -> [ptr<i32>; 1] => value;",
                "fn invalid(value: [[i32]; 1]) -> [[i32]; 1] => value;",
            });
            each(sources, std::identity {}, [&](const auto& source) noexcept {
                const auto program = analyze_test_program(std::string(source));
                expect(program.declarations().functions().size() == 1uz);
            });
        };

    "Constant arrays: execution preserves the ownership publication gate after Take"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(R"(
        const fn moved() -> i32 {
            let value = [1, 2];
            let owner = &&value;
            return value[0];
        }
        const result = moved();
    )");
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
            expect_no_diagnostic(diagnostics, DiagnosticCode::ConstEvaluation);
        };
});

} // namespace
