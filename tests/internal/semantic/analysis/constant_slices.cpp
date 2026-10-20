module carven:test.internal.semantic.analysis.constant_slices;

import :diagnostics.code;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Constant slices: completed arrays freeze without changing their element types"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const fn build(offset: i32) -> [i32; 4] {
            var row = [0, 0, 0, 0];
            for index in 0..4 { row[index] = offset + index * index; }
            return row;
        }
        const table: [i32] = build(3);
        const explicit_view = build(3).slice();
        const empty: [u8] = [];
        const rows: [[i32; 2]] = [[1, 2], [3, 4]];
        const text: [str] = ["first", "second"];
        const tail = table.slice(1, 3);
        const selected = tail[1];
        const nested = rows[1][0];
        const length = table.length();
        const ending = table.slice(4, 4);
        const is_empty = ending.is_empty();
        const skipped = false && (build(8).slice()[0] == 8);
    )");
            auto checked = 0uz;
            const auto values = PublishedConstantValues(program);
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
                    if (!expect(value != nullptr)) {
                        return;
                    }
                    expect_equal(
                        value->as_unsigned(),
                        name == "selected"     ? 7u
                            : name == "nested" ? 3u
                                               : 4u
                    );
                    ++checked;
                } else if (name == "is_empty" || name == "skipped") {
                    const auto* value = std::get_if<BooleanConstant>(&fact.value);
                    if (!expect(value != nullptr)) {
                        return;
                    }
                    expect(value->value == (name == "is_empty"));
                    ++checked;
                } else {
                    const auto value =
                        execution_compound_view(values, ExecutionValue(declaration.value));
                    if (!expect(value.has_value())) {
                        return;
                    }
                    const auto* type =
                        std::get_if<SliceTypeValue>(&program.types().type(fact.type).value);
                    if (!expect(type != nullptr)) {
                        return;
                    }
                    value->elements.visit([&](const auto& elements) noexcept {
                        for (auto index = 0uz; index < elements.size(); ++index) {
                            expect(
                                execution_value_type(values, elements[index])
                                == ConstructionTypeRef(type->element)
                            );
                        }
                    });
                    if (name == "empty" || name == "ending") {
                        expect(value->size() == 0uz);
                    }
                    if (name == "rows") {
                        const auto* row =
                            std::get_if<ArrayTypeValue>(&program.types().type(type->element).value);
                        if (!expect(row != nullptr)) {
                            return;
                        }
                        expect(row->extent == 2u);
                    }
                    ++checked;
                }
            }
            if (!expect(table.has_value())) {
                return;
            }
            expect(table == explicit_view);
            expect(checked == 12uz);
        };

    "Array queries: direct extents use ordinary constant and body execution"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
                const fn inspect() -> bool {
                    var values = [3, 5, 7];
                    let empty: [i32; 0] = [];
                    return values.length() == 3 && !values.is_empty()
                        && empty.length() == 0 && empty.is_empty();
                }
                const count = [1, 2, 3].length();
                const nonempty = [1, 2, 3].is_empty();
                const executed = inspect();
            )");
            auto checked = 0uz;
            for (const auto [id, declaration] : program.declarations().module_constants()) {
                static_cast<void>(id);
                const auto name = program.provenance().spelling(declaration.name);
                const auto& fact = program.constants().constant(declaration.value);
                if (name == "count") {
                    expect_equal(std::get<IntegerConstant>(fact.value).as_unsigned(), 3u);
                } else {
                    expect(std::get<BooleanConstant>(fact.value).value == (name == "executed"));
                }
                ++checked;
            }
            expect(checked == 3uz);
        };

    "Constant slices: type access and bounds failures remain source diagnostics"_test =
        [] static noexcept {
            struct Case final {
                std::string_view source;
                DiagnosticCode code;
            };

            constexpr auto cases = std::array {
                Case {
                    .source = "const values: [u32] = [1i32];",
                    .code = DiagnosticCode::TypeMismatch
                },
                Case {
                    .source = "const value = [1, 2].slice()[2];",
                    .code = DiagnosticCode::ConstIndexBounds
                },
                Case {
                    .source = "const value = [1, 2].slice()[-1];",
                    .code = DiagnosticCode::ConstIndexBounds
                },
                Case {
                    .source = "const value = [1, 2].slice()[true];",
                    .code = DiagnosticCode::TypeIndexInteger
                },
                Case {
                    .source = "const value = [1, 2].slice(2, 1);",
                    .code = DiagnosticCode::ConstIndexBounds
                },
                Case {
                    .source = "const value = [1, 2].slice(0, 3);",
                    .code = DiagnosticCode::ConstIndexBounds
                },
                Case {
                    .source = "const value = [1, 2].slice(true, 1);",
                    .code = DiagnosticCode::TypeMismatch
                },
                Case {
                    .source = "const value = [1, 2].slice().length(0);",
                    .code = DiagnosticCode::TypeMethodCallArity
                },
                Case {
                    .source = "const value = [1, 2].length(0);",
                    .code = DiagnosticCode::TypeMethodCallArity
                },
                Case {
                    .source = "const value = [1, 2].is_empty(0);",
                    .code = DiagnosticCode::TypeMethodCallArity
                },
                Case {
                    .source = "const a: [i32] = [1]; const equal = a == a;",
                    .code = DiagnosticCode::TypeEqualityUnsupported
                },
            };
            each(cases, &Case::source, [&](const auto& item) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(item.source));
                expect_diagnostic(diagnostics, item.code);
            });
        };

    "Constant slices: frozen storage does not extend ordinary local array borrows"_test =
        [] static noexcept {
            constexpr auto cases = std::array {
                "fn bad() { let view = [1, 2].slice(); }",
                "const source: [i32; 2] = [1, 2]; fn bad() { var copy = source; let view = copy.slice(); copy[0] = 9; }",
            };
            each(cases, std::identity {}, [&](const auto& source) noexcept {
                expect_diagnostic(
                    analyze_test_errors(source),
                    DiagnosticCode::AccessBorrowConflict
                );
            });
        };

    "Constant slices: repeated retained length queries do not consume construction work"_test =
        [] static noexcept {
            // Repeated length queries read retained storage without constructing new elements.
            auto source = std::string("const array = [");
            for (auto index = 0uz; index < 2048uz; ++index) {
                source += index == 0uz ? "0" : ",0";
            }
            source += "]; const values = array.slice(); const count = values.length()";
            for (auto index = 0uz; index < 256uz; ++index) {
                source += "+values.length()";
            }
            source += ';';
            const auto program = analyze_test_program(std::move(source));
            auto checked = false;
            for (const auto [id, declaration] : program.declarations().module_constants()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) == "count") {
                    const auto& fact = program.constants().constant(declaration.value);
                    expect(std::get<IntegerConstant>(fact.value).magnitude() == 526336uz);
                    checked = true;
                }
            }
            expect(checked);
        };
});

} // namespace
