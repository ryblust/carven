module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.constant_structs;

import :semantic.analysis.program;
import :semantic.evaluation.freeze;
import :semantic.evaluation.shape;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.text;
import :test.internal.harness.death;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

auto define_structure(ProgramDraft& draft) noexcept -> TypeID {
    const auto provenance = draft.provenance_module_at(0uz);
    const auto origin = draft.append_source_origin(draft.module_source(provenance), Span::at(0u));
    const auto module = draft.reserve_module_declaration();
    const auto structure = draft.reserve_struct_declaration();
    draft.define_declaration(
        module,
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
            .module_id = module,
            .name = draft.intern_spelling("Entry"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .fields =
                {
                    {.name = draft.intern_spelling("value"),
                     .type = draft.builtin_type(BuiltinType::I32),
                     .origin = origin},
                    {.name = draft.intern_spelling("enabled"),
                     .type = draft.builtin_type(BuiltinType::Bool),
                     .origin = origin},
                },
            .capabilities = {.equality = true},
        }
    );
    draft.finish_declaration_heads();
    return draft.intern_type({.value = StructTypeValue {.structure = structure}});
}

} // namespace

TEST_CASE("SemIR constants: typed execution fields freeze in declaration order") {
    auto fixture = ConstantEvaluationFixture();
    auto& draft = fixture.compilation;
    const auto type = define_structure(draft);
    const auto integer = draft.intern_constant(
        {.type = draft.builtin_type(BuiltinType::I32), .value = IntegerConstant::from_signed(7)}
    );
    const auto boolean = draft.intern_constant(
        {.type = draft.builtin_type(BuiltinType::Bool), .value = BooleanConstant {.value = true}}
    );
    const auto value = ExecutionAggregateValue {.type = type, .elements = {integer, boolean}};
    const auto frozen = freeze_constant_value(draft, value);
    REQUIRE(frozen.has_value());
    CHECK(draft.constant(*frozen).type == type);
    CHECK(
        std::get<StructConstant>(draft.constant(*frozen).value).fields
        == std::vector<ConstantID> {integer, boolean}
    );
    CHECK(freeze_constant_value(draft, value) == frozen);
    CHECK_FALSE(freeze_constant_value(
        draft,
        ExecutionAggregateValue {.type = type, .elements = {boolean, integer}}
    ));
    CHECK_FALSE(
        freeze_constant_value(draft, ExecutionAggregateValue {.type = type, .elements = {integer}})
    );
    CHECK_FALSE(freeze_constant_value(
        draft,
        ExecutionAggregateValue {
            .type = type,
            .elements = {ExecutionOwnedText {.bytes = "7"}, boolean}
        }
    ));
    CHECK(std::move(draft).finish().has_value());
}

TEST_CASE("SemIR constants: struct fields count toward retained aggregate size and depth") {
    auto fixture = ConstantEvaluationFixture();
    auto& draft = fixture.compilation;
    const auto entry = define_structure(draft);
    const auto table =
        draft.intern_type({.value = ArrayTypeValue {.element = entry, .extent = 21845u}});
    const auto shapes = ExecutionTypeShapes(draft);
    REQUIRE(shapes.get(table).has_value());
    CHECK(shapes.get(table)->supported);
    CHECK(shapes.get(table)->elements == 65535uz);
    const auto oversized =
        draft.intern_type({.value = ArrayTypeValue {.element = entry, .extent = 21846u}});
    REQUIRE(shapes.get(oversized).has_value());
    CHECK(shapes.get(oversized)->elements == 65537uz);
    auto nested = entry;
    for (auto level = 1uz; level < 64uz; ++level) {
        nested = draft.intern_type({.value = ArrayTypeValue {.element = nested, .extent = 1u}});
    }
    REQUIRE(shapes.get(nested).has_value());
    CHECK(shapes.get(nested)->supported);
    CHECK(shapes.get(nested)->elements == 65uz);
    nested = draft.intern_type({.value = ArrayTypeValue {.element = nested, .extent = 1u}});
    CHECK_FALSE(shapes.get(nested).has_value());
    const auto cold_shapes = ExecutionTypeShapes(draft);
    CHECK_FALSE(cold_shapes.get(nested).has_value());
}

TEST_CASE(
    "SemIR publication invariant: struct constants require exact nominal field types and arity"
) {
    enum class Malformation { NonStruct, Missing, Swapped, Extra };
    const auto scenarios = std::array {
        std::pair {"struct-non-struct-type", Malformation::NonStruct},
        std::pair {"struct-missing-field", Malformation::Missing},
        std::pair {"struct-swapped-fields", Malformation::Swapped},
        std::pair {"struct-extra-field", Malformation::Extra},
    };
    for (const auto& [name, malformation] : scenarios) {
        CHECK(expect_termination(name, [&]() noexcept {
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
            static_cast<void>(draft.intern_constant(
                {.type = type, .value = StructConstant {.fields = std::move(fields)}}
            ));
            static_cast<void>(std::move(draft).finish());
        }));
    }
}

TEST_CASE("SemIR constants invariant: struct fields belong to the receiving constant store") {
    CHECK(expect_termination("struct-foreign-field", []() static noexcept {
        auto fixture = ConstantEvaluationFixture();
        auto other = ConstantEvaluationFixture();
        auto& draft = fixture.compilation;
        const auto type = define_structure(draft);
        const auto foreign = other.compilation.intern_constant({
            .type = other.compilation.builtin_type(BuiltinType::I32),
            .value = IntegerConstant::from_signed(1),
        });
        static_cast<void>(
            draft.intern_constant({.type = type, .value = StructConstant {.fields = {foreign}}})
        );
    }));
}
