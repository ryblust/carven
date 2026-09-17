module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constant_structs;

import :diagnostics.code;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Constant structs: field order and nominal types survive array and slice publication") {
    const auto program = analyze_test_program(R"(
        const values: [Entry] = make();
        const selected = values[0].key;
        struct Entry { key: i32, enabled: bool }
        const fn make() -> [Entry; 2] {
            var entry = Entry { enabled: true, key: 3 };
            let copy = entry;
            entry.key = 9;
            return [copy, entry];
        }
    )");
    auto inspected = false;
    for (const auto declaration : program.declarations().module_constants()) {
        if (program.provenance().spelling(declaration.value.name) != "values") {
            continue;
        }
        const auto& fact = program.constants().constant(declaration.value.value);
        const auto* slice = std::get_if<SliceConstant>(&fact.value);
        REQUIRE(slice != nullptr);
        REQUIRE(slice->elements.size() == 2uz);
        const auto* type = std::get_if<SliceTypeValue>(&program.types().type(fact.type).value);
        REQUIRE(type != nullptr);
        CHECK(std::holds_alternative<StructTypeValue>(program.types().type(type->element).value));
        const auto keys = std::array {3ll, 9ll};
        for (auto index = 0uz; index < slice->elements.size(); ++index) {
            const auto& entry = program.constants().constant(slice->elements[index]);
            CHECK(entry.type == type->element);
            const auto* fields = std::get_if<StructConstant>(&entry.value);
            REQUIRE(fields != nullptr);
            REQUIRE(fields->fields.size() == 2uz);
            CHECK(
                std::get<IntegerConstant>(program.constants().constant(fields->fields[0]).value)
                    .as_signed()
                == keys[index]
            );
            CHECK(
                std::get<BooleanConstant>(program.constants().constant(fields->fields[1]).value)
                    .value
            );
        }
        inspected = true;
    }
    CHECK(inspected);
}

TEST_CASE("Constant structs: unsupported fields are rejected even in unused definitions") {
    const auto types = std::to_array<std::string_view>({"ptr<i32>", "[i32]"});
    for (const auto type : types) {
        CAPTURE(type);
        const auto diagnostics = analyze_test_errors(
            std::format(
                "struct Inner {{ value: {} }} struct Outer {{ inner: Inner }} "
                "const fn identity(value: Outer) -> Outer => value;",
                type
            )
        );
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstAdmission));
    }
}

TEST_CASE("Constant structs: source field errors and nominal mismatches retain their contracts") {
    struct Scenario final {
        std::string_view source;
        DiagnosticCode code;
    };

    const auto cases = std::to_array<Scenario>({
        {"struct Entry { value: i32 } const value = Entry {};", DiagnosticCode::TypeConstructArity},
        {"struct Entry { value: i32 } const value = Entry { extra: 1 };",
         DiagnosticCode::TypeConstructUnknownField},
        {"struct Entry { value: i32 } const value = Entry { value: 1, value: 2 };",
         DiagnosticCode::TypeConstructDuplicateField},
        {"struct Entry { value: i32 } const value = Entry { 1 }; const missing = value.missing;",
         DiagnosticCode::TypeMemberUnresolved},
        {"struct Entry { value: i32 } const value = true || (Entry { 1 }.missing == 0);",
         DiagnosticCode::TypeMemberUnresolved},
        {"struct A { value: i32 } struct B { value: i32 } const value: B = A { 1 };",
         DiagnosticCode::TypeMismatch},
        {"struct Entry { value: i32 } const table: [Entry] = [Entry { 1 }]; const bad = table[1].value;",
         DiagnosticCode::ConstIndexBounds},
    });
    for (const auto& scenario : cases) {
        CAPTURE(scenario.source);
        CHECK(contains_diagnostic_code(
            analyze_test_errors(std::string(scenario.source)),
            scenario.code
        ));
    }
}

TEST_CASE("Constant structs: ownership rejects use after Take and borrowed local text") {
    const auto moved = analyze_test_errors(R"(
        struct Entry { value: i32 }
        const fn bad() -> i32 {
            var entry = Entry { 1 };
            let taken = &&entry;
            return entry.value;
        }
        const result = bad();
    )");
    CHECK(contains_diagnostic_code(moved, DiagnosticCode::AccessUnavailable));
    CHECK_FALSE(contains_diagnostic_code(moved, DiagnosticCode::ConstEvaluation));
    const auto borrowed = analyze_test_errors(R"(
        struct Entry { value: str }
        const fn bad() -> Entry {
            let text: String = "owned";
            return Entry { text.as_str() };
        }
        const result = bad();
    )");
    CHECK(contains_diagnostic_code(borrowed, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Constant structs: nesting limits include previously visited field types") {
    auto nested = std::string("Base");
    for (auto level = 0uz; level < 61uz; ++level) {
        nested = std::format("[{}; 1]", nested);
    }
    const auto source = [](std::string_view field) static noexcept {
        return std::format(
            "struct Base {{ values: [i32; 2] }} struct Root {{ known: Base, deep: {} }} "
            "const fn identity(value: Root) -> Root => value;",
            field
        );
    };
    static_cast<void>(analyze_test_program(source(nested)));
    const auto diagnostics = analyze_test_errors(source(std::format("[{}; 1]", nested)));
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstAdmission));
}
