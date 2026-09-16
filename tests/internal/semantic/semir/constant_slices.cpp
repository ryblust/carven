module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.constant_slices;

import :compiler.request;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.program;
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
    const auto source = sources.append_virtual("constant-slices.cv", "");
    REQUIRE(source.has_value());
    auto path = CanonicalModulePath::from_value("constant.slices");
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

auto slice_type(ProgramDraft& draft, TypeID element) noexcept -> TypeID {
    return draft.intern_type({.value = SliceTypeValue {.element = element}});
}

} // namespace

TEST_CASE("SemIR constants: slices intern typed ordered contents independently of host storage") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto draft = begin_compilation(sources, diagnostics);
    const auto integer = draft.intern_builtin_type(BuiltinType::I32);
    const auto boolean = draft.intern_builtin_type(BuiltinType::Bool);
    const auto one = draft.intern_constant({
        .type = integer,
        .value = IntegerConstant::from_signed(1),
    });
    const auto two = draft.intern_constant({
        .type = integer,
        .value = IntegerConstant::from_signed(2),
    });
    const auto type = slice_type(draft, integer);
    const auto fact = ConstantFact {
        .type = type,
        .value = SliceConstant {.elements = {one, two, one}},
    };
    const auto id = draft.intern_constant(fact);
    auto separately_stored = std::vector<ConstantID> {one, two, one};
    separately_stored.reserve(32uz);
    CHECK(
        draft.intern_constant({
            .type = type,
            .value = SliceConstant {.elements = std::move(separately_stored)},
        })
        == id
    );
    CHECK(
        draft.intern_constant({
            .type = type,
            .value = SliceConstant {.elements = {two, one, one}},
        })
        != id
    );
    CHECK(
        draft.intern_constant({
            .type = type,
            .value = SliceConstant {.elements = {one, two}},
        })
        != id
    );
    CHECK(id.owner() == draft.identity());
    const auto empty = draft.intern_constant({
        .type = type,
        .value = SliceConstant {.elements = {}},
    });
    CHECK(
        draft.intern_constant({
            .type = slice_type(draft, boolean),
            .value = SliceConstant {.elements = {}},
        })
        != empty
    );
    const auto program = std::move(draft).finish();
    REQUIRE(program.has_value());
    CHECK(program->constants().constant(id) == fact);
    const auto& empty_fact = program->constants().constant(empty);
    CHECK(empty_fact.type == type);
    const auto* empty_value = std::get_if<SliceConstant>(&empty_fact.value);
    REQUIRE(empty_value != nullptr);
    CHECK(empty_value->elements.empty());
}

TEST_CASE("SemIR constants: slices publish scalar text and nested fixed-array elements") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto draft = begin_compilation(sources, diagnostics);
    const auto integer = draft.intern_builtin_type(BuiltinType::I32);
    const auto one = draft.intern_constant({
        .type = integer,
        .value = IntegerConstant::from_signed(1),
    });
    const auto row_type = array_type(draft, integer, 2u);
    const auto row = draft.intern_constant({
        .type = row_type,
        .value = ArrayConstant {.elements = {one, one}},
    });
    const auto nested_type = array_type(draft, row_type, 1u);
    const auto nested = draft.intern_constant({
        .type = nested_type,
        .value = ArrayConstant {.elements = {row}},
    });
    const auto elements = std::array {
        one,
        draft.intern_constant({
            .type = draft.intern_builtin_type(BuiltinType::Bool),
            .value = BooleanConstant {.value = true},
        }),
        draft.intern_constant({
            .type = draft.intern_builtin_type(BuiltinType::Char),
            .value = CharacterConstant {.scalar = U'我'},
        }),
        draft.intern_constant({
            .type = draft.intern_builtin_type(BuiltinType::Str),
            .value = StringConstant {.value = draft.intern_spelling("hello")},
        }),
        nested,
    };
    auto slices = std::vector<ConstantID>();
    for (const auto element : elements) {
        const auto& type = slice_type(draft, draft.constant(element).type);
        slices.push_back(draft.intern_constant({
            .type = type,
            .value = SliceConstant {.elements = {element, element}},
        }));
    }
    const auto program = std::move(draft).finish();
    REQUIRE(program.has_value());
    for (const auto [id, element] : std::views::zip(slices, elements)) {
        const auto& fact = program->constants().constant(id);
        const auto* type = std::get_if<SliceTypeValue>(&program->types().type(fact.type).value);
        REQUIRE(type != nullptr);
        CHECK(type->element == program->constants().constant(element).type);
        const auto* value = std::get_if<SliceConstant>(&fact.value);
        REQUIRE(value != nullptr);
        const auto expected = std::vector<ConstantID> {element, element};
        CHECK(value->elements == expected);
    }
}

TEST_CASE("SemIR publication invariant: slice constants require exact element and child facts") {
    enum class Malformation {
        NonSliceType,
        ArrayType,
        ElementType,
        NestedElementType,
        ChildFact,
        NestedChildShape,
    };

    struct Scenario final {
        std::string_view name;
        Malformation malformation;
    };

    const auto scenarios = std::array {
        Scenario {.name = "slice-constant-non-slice", .malformation = Malformation::NonSliceType},
        Scenario {.name = "slice-constant-array-type", .malformation = Malformation::ArrayType},
        Scenario {.name = "slice-constant-element-type", .malformation = Malformation::ElementType},
        Scenario {
            .name = "slice-constant-nested-type",
            .malformation = Malformation::NestedElementType
        },
        Scenario {.name = "slice-constant-child-fact", .malformation = Malformation::ChildFact},
        Scenario {
            .name = "slice-constant-child-shape",
            .malformation = Malformation::NestedChildShape
        },
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        CHECK(expect_termination(scenario.name, [&] noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics);
            const auto integer = draft.intern_builtin_type(BuiltinType::I32);
            const auto boolean = draft.intern_builtin_type(BuiltinType::Bool);
            auto child = draft.intern_constant({
                .type = integer,
                .value = IntegerConstant::from_signed(1),
            });
            auto type = slice_type(draft, integer);
            switch (scenario.malformation) {
                case Malformation::NonSliceType: type = integer; break;
                case Malformation::ArrayType:    type = array_type(draft, integer, 1u); break;
                case Malformation::ElementType:  type = slice_type(draft, boolean); break;
                case Malformation::NestedElementType:
                    child = draft.intern_constant({
                        .type = array_type(draft, integer, 1u),
                        .value = ArrayConstant {.elements = {child}},
                    });
                    type = slice_type(draft, array_type(draft, boolean, 1u));
                    break;
                case Malformation::ChildFact:
                    child = draft.intern_constant({
                        .type = boolean,
                        .value = IntegerConstant::from_signed(1),
                    });
                    type = slice_type(draft, boolean);
                    break;
                case Malformation::NestedChildShape:
                    type = array_type(draft, integer, 2u);
                    child = draft.intern_constant({
                        .type = type,
                        .value = ArrayConstant {.elements = {child}},
                    });
                    type = slice_type(draft, type);
                    break;
            }
            static_cast<void>(draft.intern_constant({
                .type = type,
                .value = SliceConstant {.elements = {child}},
            }));
            static_cast<void>(std::move(draft).finish());
        }));
    }
}

TEST_CASE("SemIR constants invariant: slice children already belong to the same store") {
    const auto scenarios = std::array {false, true};
    for (const auto foreign : scenarios) {
        CHECK(expect_termination(
            foreign ? "slice-constant-foreign-child" : "slice-constant-unavailable-child",
            [&] noexcept {
                auto sources = SourceManager();
                auto diagnostics = DiagnosticSink();
                auto draft = begin_compilation(sources, diagnostics);
                auto other_sources = SourceManager();
                auto other_diagnostics = DiagnosticSink();
                const auto other = begin_compilation(other_sources, other_diagnostics);
                const auto integer = draft.intern_builtin_type(BuiltinType::I32);
                auto alternate = MutableProgramTable<ConstantFact, ConstantID>(
                    foreign ? other.identity() : draft.identity()
                );
                const auto child = alternate.add({
                    .type = integer,
                    .value = IntegerConstant::from_signed(1),
                });
                static_cast<void>(draft.intern_constant({
                    .type = slice_type(draft, integer),
                    .value = SliceConstant {.elements = {child}},
                }));
            }
        ));
    }
}
