module carven:test.internal.semantic.analysis.constant_structs;

import :diagnostics.code;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Constant structs: field order and nominal types survive array and slice publication",
        [] static noexcept {
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
                if (!ct::expect(slice != nullptr)) {
                    return;
                }
                if (!ct::expect(slice->elements.size() == 2uz)) {
                    return;
                }
                const auto* type =
                    std::get_if<SliceTypeValue>(&program.types().type(fact.type).value);
                if (!ct::expect(type != nullptr)) {
                    return;
                }
                ct::expect(
                    std::holds_alternative<StructTypeValue>(
                        program.types().type(type->element).value
                    )
                );
                const auto keys = std::array {3ll, 9ll};
                for (auto index = 0uz; index < slice->elements.size(); ++index) {
                    const auto& entry = program.constants().constant(slice->elements[index]);
                    ct::expect(entry.type == type->element);
                    const auto* fields = std::get_if<StructConstant>(&entry.value);
                    if (!ct::expect(fields != nullptr)) {
                        return;
                    }
                    if (!ct::expect(fields->fields.size() == 2uz)) {
                        return;
                    }
                    ct::expect(
                        std::get<IntegerConstant>(
                            program.constants().constant(fields->fields[0]).value
                        )
                            .as_signed()
                        == keys[index]
                    );
                    ct::expect(
                        std::get<BooleanConstant>(
                            program.constants().constant(fields->fields[1]).value
                        )
                            .value
                    );
                }
                inspected = true;
            }
            ct::expect(inspected);
        }
    );

    ct::test(
        "Constant structs: unused functions do not require executable field types",
        [] static noexcept {
            const auto types = std::to_array<std::string_view>({"ptr<i32>", "[i32]"});
            for (const auto type : types) {
                const auto program = analyze_test_program(
                    std::format(
                        "struct Inner {{ value: {} }} struct Outer {{ inner: Inner }} "
                        "fn identity(value: Outer) -> Outer => value;",
                        type
                    )
                );
                ct::expect(program.declarations().functions().size() == 1uz).note("type = ", type);
            }
        }
    );

    ct::test(
        "Constant structs: source field errors and nominal mismatches retain their contracts",
        [] static noexcept {
            struct Scenario final {
                std::string_view source;
                DiagnosticCode code;
            };

            const auto cases = std::to_array<Scenario>({
                {"enum Choice { Item } struct Entry { value: Choice } const value = Entry {};",
                 DiagnosticCode::TypeDefaultInitialization},
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
            ct::each(cases, &Scenario::source, [&](const auto& scenario) noexcept {
                ct::expect_diagnostic(
                    analyze_test_errors(std::string(scenario.source)),
                    scenario.code
                );
            });
        }
    );

    ct::test(
        "Constant structs: definitions reject use after Take and escaping borrowed fields",
        [] static noexcept {
            const auto moved = analyze_test_errors(R"(
        struct Entry { value: i32 }
        const fn bad() -> i32 {
            var entry = Entry { 1 };
            let taken = &&entry;
            return entry.value;
        }
    )");
            ct::expect_diagnostic(moved, DiagnosticCode::AccessUnavailable);
            ct::expect_no_diagnostic(moved, DiagnosticCode::ConstEvaluation);
            const auto borrowed = analyze_test_errors(R"(
        struct Entry { value: str }
        const fn bad() -> Entry {
            let text: String = "owned";
            return Entry { text.as_str() };
        }
    )");
            ct::expect_diagnostic(borrowed, DiagnosticCode::AccessBorrowConflict);
        }
    );

    ct::test(
        "Constant structs: unused deep field types do not consume execution budget",
        [] static noexcept {
            auto nested = std::string("Base");
            for (auto level = 0uz; level < 61uz; ++level) {
                nested = std::format("[{}; 1]", nested);
            }
            const auto source = [](std::string_view field) static noexcept {
                return std::format(
                    "struct Base {{ values: [i32; 2] }} struct Root {{ known: Base, deep: {} }} "
                    "fn identity(value: Root) -> Root => value;",
                    field
                );
            };
            static_cast<void>(analyze_test_program(source(nested)));
            static_cast<void>(analyze_test_program(source(std::format("[{}; 1]", nested))));
        }
    );
});

} // namespace
