module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constant_slices;

import :diagnostics.code;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Const slices: completed arrays freeze without changing their element types") {
    const auto program = analyze_test_program(R"(
        const fn build(offset: i32) -> [i32; 4] {
            var row = [0, 0, 0, 0];
            for index in 0..4 { row[index] = offset + index * index; }
            return row;
        }
        const table: [i32] = build(3);
        const explicit_view = build(3).as_slice();
        const empty: [u8] = [];
        const rows: [[i32; 2]] = [[1, 2], [3, 4]];
        const text: [str] = ["first", "second"];
        const tail = table.slice(1, 3);
        const selected = tail[1];
        const nested = rows[1][0];
        const length = table.len();
        const ending = table.slice(4, 4);
        const is_empty = ending.is_empty();
        const skipped = false && (build(8).as_slice()[0] == 8);
    )");
    auto checked = 0uz;
    auto table = std::optional<ConstantID>();
    auto explicit_view = std::optional<ConstantID>();
    for (const auto [id, declaration] : program.declarations().module_constants()) {
        static_cast<void>(id);
        const auto name = program.provenance().spelling(declaration.name);
        const auto& fact = program.constants().constant(declaration.value);
        if (name == "table") {
            table = declaration.value;
        }
        if (name == "explicit_view") {
            explicit_view = declaration.value;
        }
        if (name == "selected" || name == "nested" || name == "length") {
            const auto* value = std::get_if<IntegerConstant>(&fact.value);
            REQUIRE(value != nullptr);
            CHECK(value->as_unsigned() == (name == "selected" ? 7u : name == "nested" ? 3u : 4u));
            ++checked;
        } else if (name == "is_empty" || name == "skipped") {
            const auto* value = std::get_if<BooleanConstant>(&fact.value);
            REQUIRE(value != nullptr);
            CHECK(value->value == (name == "is_empty"));
            ++checked;
        } else {
            const auto* value = std::get_if<SliceConstant>(&fact.value);
            REQUIRE(value != nullptr);
            const auto* type = std::get_if<SliceTypeValue>(&program.types().type(fact.type).value);
            REQUIRE(type != nullptr);
            for (const auto child : value->elements) {
                CHECK(program.constants().constant(child).type == type->element);
            }
            if (name == "empty" || name == "ending") {
                CHECK(value->elements.empty());
            }
            if (name == "rows") {
                const auto* row =
                    std::get_if<ArrayTypeValue>(&program.types().type(type->element).value);
                REQUIRE(row != nullptr);
                CHECK(row->extent == 2u);
            }
            ++checked;
        }
    }
    REQUIRE(table.has_value());
    CHECK(table == explicit_view);
    CHECK(checked == 12uz);
}

TEST_CASE("Const slices: type access and bounds failures remain source diagnostics") {
    struct Case final {
        std::string_view source;
        DiagnosticCode code;
    };

    const auto cases = std::array {
        Case {.source = "const values: [u32] = [1i32];", .code = DiagnosticCode::TypeMismatch},
        Case {
            .source = "const values: [String] = [\"text\"];",
            .code = DiagnosticCode::ConstInitializer
        },
        Case {
            .source = "const value = [1, 2].as_slice()[2];",
            .code = DiagnosticCode::ConstIndexBounds
        },
        Case {
            .source = "const value = [1, 2].as_slice()[-1];",
            .code = DiagnosticCode::ConstIndexBounds
        },
        Case {
            .source = "const value = [1, 2].as_slice()[true];",
            .code = DiagnosticCode::TypeIndexInteger
        },
        Case {
            .source = "const value = [1, 2].as_slice().slice(2, 1);",
            .code = DiagnosticCode::ConstIndexBounds
        },
        Case {
            .source = "const value = [1, 2].as_slice().slice(0, 3);",
            .code = DiagnosticCode::ConstIndexBounds
        },
        Case {
            .source = "const value = [1, 2].as_slice().slice(true, 1);",
            .code = DiagnosticCode::TypeMismatch
        },
        Case {
            .source = "const value = [1, 2].as_slice().len(0);",
            .code = DiagnosticCode::TypeMethodCallArity
        },
        Case {
            .source = "const a: [i32] = [1]; const equal = a == a;",
            .code = DiagnosticCode::TypeEqualityUnsupported
        },
    };
    for (const auto& item : cases) {
        INFO(item.source);
        const auto diagnostics = analyze_test_errors(std::string(item.source));
        CHECK(contains_diagnostic_code(diagnostics, item.code));
    }
}

TEST_CASE("Const slices: frozen storage does not extend ordinary local array borrows") {
    const auto cases = std::array {
        "fn bad() -> [i32] { let local = [1, 2]; return local; }",
        "fn bad() { let view = [1, 2].as_slice(); }",
        "const source: [i32; 2] = [1, 2]; fn bad() { var copy = source; let view = copy.as_slice(); copy[0] = 9; }",
    };
    for (const auto source : cases) {
        INFO(source);
        CHECK(contains_diagnostic_code(
            analyze_test_errors(source),
            DiagnosticCode::AccessBorrowConflict
        ));
    }
}

TEST_CASE("Const slices: repeated retained length queries do not consume construction work") {
    // Repeated length queries read retained storage without constructing new elements.
    auto source = std::string("const array = [");
    for (auto index = 0uz; index < 2048uz; ++index) {
        source += index == 0uz ? "0" : ",0";
    }
    source += "]; const values = array.as_slice(); const count = values.len()";
    for (auto index = 0uz; index < 256uz; ++index) {
        source += "+values.len()";
    }
    source += ";";
    const auto program = analyze_test_program(std::move(source));
    auto checked = false;
    for (const auto [id, declaration] : program.declarations().module_constants()) {
        static_cast<void>(id);
        if (program.provenance().spelling(declaration.name) == "count") {
            const auto& fact = program.constants().constant(declaration.value);
            CHECK(std::get<IntegerConstant>(fact.value).magnitude() == 526336uz);
            checked = true;
        }
    }
    CHECK(checked);
}
