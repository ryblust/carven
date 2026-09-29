module carven:test.internal.semantic.semir.constant_slices;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("constant-slices.cv", "");
    ct::require(source.has_value());
    auto path = CanonicalModulePath::from_value("constant.slices");
    ct::require(path.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = std::move(*path),
    }};
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    ct::require(syntax.has_value());
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = draft.provenance_module_at(0uz);
    const auto origin =
        draft.append_source_origin(draft.module_source(provenance_module), Span::at(0u));
    const auto module_id = draft.reserve_module_declaration();
    draft.define_declaration(
        module_id,
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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "SemIR constants: slices intern typed ordered contents independently of host storage",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics);
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto boolean = draft.builtin_type(BuiltinType::Bool);
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
            ct::expect(
                draft.intern_constant({
                    .type = type,
                    .value = SliceConstant {.elements = std::move(separately_stored)},
                })
                == id
            );
            ct::expect(
                draft.intern_constant({
                    .type = type,
                    .value = SliceConstant {.elements = {two, one, one}},
                })
                != id
            );
            ct::expect(
                draft.intern_constant({
                    .type = type,
                    .value = SliceConstant {.elements = {one, two}},
                })
                != id
            );
            ct::expect(id.owner() == draft.identity());
            const auto empty = draft.intern_constant({
                .type = type,
                .value = SliceConstant {.elements = {}},
            });
            ct::expect(
                draft.intern_constant({
                    .type = slice_type(draft, boolean),
                    .value = SliceConstant {.elements = {}},
                })
                != empty
            );
            const auto program = std::move(draft).finish();
            if (!ct::expect(program.has_value())) {
                return;
            }
            ct::expect(program->constants().constant(id) == fact);
            const auto& empty_fact = program->constants().constant(empty);
            ct::expect(empty_fact.type == type);
            const auto* empty_value = std::get_if<SliceConstant>(&empty_fact.value);
            if (!ct::expect(empty_value != nullptr)) {
                return;
            }
            ct::expect(empty_value->elements.empty());
        }
    );

    ct::test(
        "SemIR constants: slices publish scalar text and nested fixed-array elements",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics);
            const auto integer = draft.builtin_type(BuiltinType::I32);
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
                    .type = draft.builtin_type(BuiltinType::Bool),
                    .value = BooleanConstant {.value = true},
                }),
                draft.intern_constant({
                    .type = draft.builtin_type(BuiltinType::Char),
                    .value = CharacterConstant {.scalar = U'我'},
                }),
                draft.intern_constant({
                    .type = draft.builtin_type(BuiltinType::Str),
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
            if (!ct::expect(program.has_value())) {
                return;
            }
            for (const auto [id, element] : std::views::zip(slices, elements)) {
                const auto& fact = program->constants().constant(id);
                const auto* type =
                    std::get_if<SliceTypeValue>(&program->types().type(fact.type).value);
                if (!ct::expect(type != nullptr)) {
                    continue;
                }
                ct::expect(type->element == program->constants().constant(element).type);
                const auto* value = std::get_if<SliceConstant>(&fact.value);
                if (!ct::expect(value != nullptr)) {
                    continue;
                }
                const auto expected = std::vector<ConstantID> {element, element};
                ct::expect(value->elements == expected);
            }
        }
    );

    ct::test(
        "SemIR publication invariant: slice constants require exact element and child facts",
        [] static noexcept {
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
                Scenario {
                    .name = "slice-constant-non-slice",
                    .malformation = Malformation::NonSliceType
                },
                Scenario {
                    .name = "slice-constant-array-type",
                    .malformation = Malformation::ArrayType
                },
                Scenario {
                    .name = "slice-constant-element-type",
                    .malformation = Malformation::ElementType
                },
                Scenario {
                    .name = "slice-constant-nested-type",
                    .malformation = Malformation::NestedElementType
                },
                Scenario {
                    .name = "slice-constant-child-fact",
                    .malformation = Malformation::ChildFact
                },
                Scenario {
                    .name = "slice-constant-child-shape",
                    .malformation = Malformation::NestedChildShape
                },
            };
            ct::each(scenarios, &Scenario::name, [&](const auto& scenario) noexcept {
                auto sources = SourceManager();
                auto diagnostics = DiagnosticSink();
                auto draft = begin_compilation(sources, diagnostics);
                ct::expect(expect_termination(scenario.name, [&] noexcept {
                    const auto integer = draft.builtin_type(BuiltinType::I32);
                    const auto boolean = draft.builtin_type(BuiltinType::Bool);
                    auto child = draft.intern_constant({
                        .type = integer,
                        .value = IntegerConstant::from_signed(1),
                    });
                    auto type = slice_type(draft, integer);
                    switch (scenario.malformation) {
                        case Malformation::NonSliceType: type = integer; break;
                        case Malformation::ArrayType: type = array_type(draft, integer, 1u); break;
                        case Malformation::ElementType: type = slice_type(draft, boolean); break;
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
            });
        }
    );

    ct::test(
        "SemIR constants invariant: slice children already belong to the same store",
        [] static noexcept {
            const auto scenarios = std::array {false, true};
            ct::each(
                scenarios,
                [](bool foreign) static noexcept -> std::string_view {
                    return foreign ? "foreign child" : "unavailable child";
                },
                [&](const auto& foreign) noexcept {
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
                    ct::expect(expect_termination(
                        foreign ? "slice-constant-foreign-child"
                                : "slice-constant-unavailable-child",
                        [&] noexcept {
                            static_cast<void>(draft.intern_constant({
                                .type = slice_type(draft, integer),
                                .value = SliceConstant {.elements = {child}},
                            }));
                        }
                    ));
                }
            );
        }
    );
});

} // namespace
