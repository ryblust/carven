module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.constant_arrays;

import :compiler.request;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import std;

namespace {

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("constant-arrays.cv", "");
    REQUIRE(source.has_value());
    auto path = CanonicalModulePath::from_value("constant.arrays");
    REQUIRE(path.has_value());
    const auto inputs = std::array {CompilationModuleInput {
        .source_id = *source,
        .module_path = std::move(*path),
    }};
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = draft.provenance_module_at(0uz);
    const auto origin =
        draft.append_source_origin(draft.module_source(provenance_module), Span::at(0u));
    const auto module = draft.reserve_module_declaration();
    draft.define_declaration(
        module,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        }
    );
    draft.finish_declaration_heads();
    return draft;
}

auto array_type(ProgramDraft& draft, TypeID element, std::uint64_t extent) noexcept -> TypeID {
    return draft.intern_type({.value = ArrayTypeValue {.element = element, .extent = extent}});
}

} // namespace

TEST_CASE("SemIR constants: nested arrays preserve canonical type and value identity") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto draft = begin_compilation(sources, diagnostics);
    const auto integer = draft.builtin_type(BuiltinType::I32);
    const auto element = draft.intern_constant({
        .type = integer,
        .value = IntegerConstant::from_signed(42),
    });
    const auto row_type = array_type(draft, integer, 2u);
    const auto row = ConstantFact {
        .type = row_type,
        .value = ArrayConstant {.elements = {element, element}},
    };
    const auto row_id = draft.intern_constant(row);
    CHECK(draft.intern_constant(row) == row_id);
    const auto nested = ConstantFact {
        .type = array_type(draft, row_type, 1u),
        .value = ArrayConstant {.elements = {row_id}},
    };
    const auto nested_id = draft.intern_constant(nested);
    CHECK(draft.intern_constant(nested) == nested_id);
    CHECK(constant_value_equal(draft, nested.value, nested.value));
    const auto empty_type = array_type(draft, integer, 0u);
    const auto empty = draft.intern_constant({
        .type = empty_type,
        .value = ArrayConstant {.elements = {}},
    });
    const auto program = std::move(draft).finish();
    REQUIRE(program.has_value());
    CHECK(program->constants().constant(nested_id) == nested);
    CHECK(program->constants().constant(empty).type == empty_type);
}

TEST_CASE("SemIR publication invariant: array constants match shape and exact element type") {
    enum class Malformation { NonArrayType, Extent, ElementType, NestedElementType };

    struct Scenario final {
        std::string_view name;
        Malformation malformation;
    };

    const auto scenarios = std::array {
        Scenario {.name = "array-constant-non-array", .malformation = Malformation::NonArrayType},
        Scenario {.name = "array-constant-extent", .malformation = Malformation::Extent},
        Scenario {.name = "array-constant-element", .malformation = Malformation::ElementType},
        Scenario {.name = "array-constant-nested", .malformation = Malformation::NestedElementType},
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        CHECK(expect_termination(scenario.name, [&] noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics);
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto boolean = draft.builtin_type(BuiltinType::Bool);
            auto child = draft.intern_constant({
                .type = integer,
                .value = IntegerConstant::from_signed(1),
            });
            auto type = array_type(draft, integer, 1u);
            switch (scenario.malformation) {
                case Malformation::NonArrayType: type = integer; break;
                case Malformation::Extent:       type = array_type(draft, integer, 2u); break;
                case Malformation::ElementType:  type = array_type(draft, boolean, 1u); break;
                case Malformation::NestedElementType:
                    child = draft.intern_constant({
                        .type = type,
                        .value = ArrayConstant {.elements = {child}},
                    });
                    type = array_type(draft, array_type(draft, boolean, 1u), 1u);
                    break;
            }
            static_cast<void>(draft.intern_constant({
                .type = type,
                .value = ArrayConstant {.elements = {child}},
            }));
            static_cast<void>(std::move(draft).finish());
        }));
    }
}

TEST_CASE("SemIR constants: array language equality recursively compares numeric values") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto draft = begin_compilation(sources, diagnostics);
    const auto number = draft.builtin_type(BuiltinType::F64);
    const auto positive =
        draft.intern_constant({.type = number, .value = F64Constant {.value = 0.0}});
    const auto negative =
        draft.intern_constant({.type = number, .value = F64Constant {.value = -0.0}});
    REQUIRE(positive != negative);
    const auto array = array_type(draft, number, 1u);
    const auto left = draft.intern_constant({
        .type = array,
        .value = ArrayConstant {.elements = {positive}},
    });
    const auto right = draft.intern_constant({
        .type = array,
        .value = ArrayConstant {.elements = {negative}},
    });
    CHECK(left != right);
    CHECK(constant_value_equal(
        draft,
        ArrayConstant {.elements = {left}},
        ArrayConstant {.elements = {right}}
    ));
    const auto not_a_number = draft.intern_constant({
        .type = number,
        .value = F64Constant {.value = std::numeric_limits<double>::quiet_NaN()},
    });
    CHECK_FALSE(constant_value_equal(
        draft,
        ArrayConstant {.elements = {not_a_number}},
        ArrayConstant {.elements = {not_a_number}}
    ));
}

TEST_CASE("SemIR constants invariant: array children already belong to the same store") {
    const auto scenarios = std::array {false, true};
    for (const auto foreign : scenarios) {
        CHECK(expect_termination(
            foreign ? "array-constant-foreign-child" : "array-constant-unavailable-child",
            [&] noexcept {
                auto sources = SourceManager();
                auto diagnostics = DiagnosticSink();
                auto draft = begin_compilation(sources, diagnostics);
                auto other_sources = SourceManager();
                auto other_diagnostics = DiagnosticSink();
                const auto other = begin_compilation(other_sources, other_diagnostics);
                const auto integer = draft.builtin_type(BuiltinType::I32);
                auto alternate = MutableProgramTable<ConstantFact, ConstantID>(
                    foreign ? other.identity() : draft.identity()
                );
                const auto child = alternate.add({
                    .type = integer,
                    .value = IntegerConstant::from_signed(1),
                });
                static_cast<void>(draft.intern_constant({
                    .type = array_type(draft, integer, 1u),
                    .value = ArrayConstant {.elements = {child}},
                }));
            }
        ));
    }
}
