module carven:test.internal.semantic.semir.constant_structs;

import :semantic.analysis.constant.freeze;
import :semantic.analysis.program;
import :semantic.evaluation.shape;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.text;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

namespace ct = carven::testing;

auto define_structure(ProgramDraft& draft) noexcept -> TypeID {
    const auto provenance = draft.provenance_module_at(0uz);
    const auto origin = draft.append_source_origin(draft.module_source(provenance), Span::at(0u));
    const auto module_id = draft.reserve_module_declaration();
    const auto structure = draft.reserve_struct_declaration();
    draft.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {structure},
        }
    );
    draft.define_declaration(
        structure,
        ConstructionStructDeclaration {
            .kind = RecordKind::Struct,
            .module_id = module_id,
            .name = draft.intern_spelling("Entry"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {
                {.name = draft.intern_spelling("value"),
                 .type = draft.builtin_type(BuiltinType::I32),
                 .origin = origin},
                {.name = draft.intern_spelling("enabled"),
                 .type = draft.builtin_type(BuiltinType::Bool),
                 .origin = origin},
            },
        }
    );
    draft.finish_declaration_heads();
    return draft.intern_type({.value = StructTypeValue {.structure = structure}});
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "SemIR constants: typed execution fields freeze in declaration order",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& draft = fixture.compilation;
            const auto type = define_structure(draft);
            const auto integer = draft.intern_constant(
                {.type = draft.builtin_type(BuiltinType::I32),
                 .value = IntegerConstant::from_signed(7)}
            );
            const auto boolean = draft.intern_constant(
                {.type = draft.builtin_type(BuiltinType::Bool),
                 .value = BooleanConstant {.value = true}}
            );
            const auto make_value = [&](std::initializer_list<ConstantID> fields) noexcept {
                auto elements = std::vector<ExecutionValue>();
                for (const auto field : fields) {
                    elements.emplace_back(field);
                }
                return ExecutionAggregateValue {.type = type, .elements = std::move(elements)};
            };
            const auto frozen = freeze_constant_value(draft, make_value({integer, boolean}));
            if (!ct::expect(frozen.has_value())) {
                return;
            }
            ct::expect(draft.constant(*frozen).type == type);
            ct::expect(
                std::get<StructConstant>(draft.constant(*frozen).value).fields
                == std::vector<ConstantID> {integer, boolean}
            );
            ct::expect(freeze_constant_value(draft, make_value({integer, boolean})) == frozen);
            ct::expect(!(freeze_constant_value(draft, make_value({boolean, integer}))));
            ct::expect(!(freeze_constant_value(draft, make_value({integer}))));
            auto invalid_fields = std::vector<ExecutionValue>();
            invalid_fields.emplace_back(ExecutionOwnedText("7"));
            invalid_fields.emplace_back(boolean);
            ct::expect(!(freeze_constant_value(
                draft,
                ExecutionAggregateValue {.type = type, .elements = std::move(invalid_fields)}
            )));
            ct::expect(std::move(draft).finish().has_value());
        }
    );

    ct::test(
        "SemIR constants: struct fields count toward retained aggregate size and depth",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& draft = fixture.compilation;
            const auto entry = define_structure(draft);
            const auto table =
                draft.intern_type({.value = ArrayTypeValue {.element = entry, .extent = 21845u}});
            const auto shapes = ExecutionTypeShapes(draft);
            if (!ct::expect(shapes.get(table).has_value())) {
                return;
            }
            ct::expect(shapes.get(table)->supported);
            ct::expect(shapes.get(table)->elements == 65535uz);
            const auto oversized =
                draft.intern_type({.value = ArrayTypeValue {.element = entry, .extent = 21846u}});
            if (!ct::expect(shapes.get(oversized).has_value())) {
                return;
            }
            ct::expect(shapes.get(oversized)->elements == 65537uz);
            auto nested = entry;
            for (auto level = 1uz; level < 64uz; ++level) {
                nested =
                    draft.intern_type({.value = ArrayTypeValue {.element = nested, .extent = 1u}});
            }
            if (!ct::expect(shapes.get(nested).has_value())) {
                return;
            }
            ct::expect(shapes.get(nested)->supported);
            ct::expect(shapes.get(nested)->elements == 65uz);
            nested = draft.intern_type({.value = ArrayTypeValue {.element = nested, .extent = 1u}});
            ct::expect(!(shapes.get(nested).has_value()));
            const auto cold_shapes = ExecutionTypeShapes(draft);
            ct::expect(!(cold_shapes.get(nested).has_value()));
        }
    );

    ct::test(
        "SemIR publication invariant: struct constants require exact nominal field types and arity",
        [] static noexcept {
            enum class Malformation { NonStruct, Missing, Swapped, Extra };
            const auto scenarios = std::array {
                std::pair {"struct-non-struct-type", Malformation::NonStruct},
                std::pair {"struct-missing-field", Malformation::Missing},
                std::pair {"struct-swapped-fields", Malformation::Swapped},
                std::pair {"struct-extra-field", Malformation::Extra},
            };
            ct::each(
                scenarios,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    const auto& [name, malformation] = entry;
                    auto fixture = ConstantEvaluationFixture();
                    auto& draft = fixture.compilation;
                    auto type = define_structure(draft);
                    const auto integer_type = draft.builtin_type(BuiltinType::I32);
                    const auto integer = draft.intern_constant(
                        {.type = integer_type, .value = IntegerConstant::from_signed(7)}
                    );
                    const auto boolean = draft.intern_constant(
                        {.type = draft.builtin_type(BuiltinType::Bool),
                         .value = BooleanConstant {.value = true}}
                    );
                    auto fields = std::vector<ConstantID> {integer, boolean};
                    switch (malformation) {
                        case Malformation::NonStruct: type = integer_type; break;
                        case Malformation::Missing:   fields.pop_back(); break;
                        case Malformation::Swapped:   std::swap(fields[0], fields[1]); break;
                        case Malformation::Extra:     fields.push_back(integer); break;
                    }
                    ct::expect(expect_termination(name, [&]() noexcept {
                        static_cast<void>(draft.intern_constant(
                            {.type = type, .value = StructConstant {.fields = std::move(fields)}}
                        ));
                        static_cast<void>(std::move(draft).finish());
                    }));
                }
            );
        }
    );

    ct::test(
        "SemIR constants invariant: struct fields belong to the receiving constant store",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto other = ConstantEvaluationFixture();
            auto& draft = fixture.compilation;
            const auto type = define_structure(draft);
            const auto foreign = other.compilation.intern_constant({
                .type = other.compilation.builtin_type(BuiltinType::I32),
                .value = IntegerConstant::from_signed(1),
            });
            ct::expect(expect_termination("struct-foreign-field", [&]() noexcept {
                static_cast<void>(draft.intern_constant(
                    {.type = type, .value = StructConstant {.fields = {foreign}}}
                ));
            }));
        }
    );
});

} // namespace
