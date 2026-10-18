module carven:test.internal.semantic.semir.constant_arrays;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("constant-arrays.cv", "");
    require(source.has_value());
    auto path = CanonicalModulePath::from_value("constant.arrays");
    require(path.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = std::move(*path),
    }};
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value());
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics, HostEffects::Deny);
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

const TestSuite suite([] static noexcept {
    "SemIR constants: tuples preserve heterogeneous element types and nested unit values"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics);
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto boolean = draft.builtin_type(BuiltinType::Bool);
            const auto unit = draft.builtin_type(BuiltinType::Void);
            const auto number =
                draft.intern_constant({.type = integer, .value = IntegerConstant::from_signed(42)});
            const auto truth =
                draft.intern_constant({.type = boolean, .value = BooleanConstant {.value = true}});
            const auto nothing = draft.intern_constant({.type = unit, .value = VoidConstant {}});
            expect(draft.intern_constant({.type = unit, .value = VoidConstant {}}) == nothing);
            const auto pair_type =
                draft.intern_type({.value = TupleTypeValue {.elements = {integer, boolean}}});
            const auto pair = ConstantFact {
                .type = pair_type,
                .value = TupleConstant {.elements = {number, truth}}
            };
            const auto pair_id = draft.intern_constant(pair);
            expect(draft.intern_constant(pair) == pair_id);
            const auto nested_type =
                draft.intern_type({.value = TupleTypeValue {.elements = {pair_type, unit}}});
            const auto nested = draft.intern_constant(
                {.type = nested_type, .value = TupleConstant {.elements = {pair_id, nothing}}}
            );
            const auto program = std::move(draft).finish();
            if (!expect(program.has_value())) {
                return;
            }
            expect(program->constants().constant(pair_id) == pair);
            const auto children = constant_children(program->constants().constant(nested).value);
            if (!expect(children.has_value())) {
                return;
            }
            expect_equal(children->size(), 2uz);
            expect((*children)[0] == pair_id);
            expect((*children)[1] == nothing);
        };

    "SemIR constants: nested arrays preserve canonical type and value identity"_test =
        [] static noexcept {
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
            expect(draft.intern_constant(row) == row_id);
            const auto nested = ConstantFact {
                .type = array_type(draft, row_type, 1u),
                .value = ArrayConstant {.elements = {row_id}},
            };
            const auto nested_id = draft.intern_constant(nested);
            expect(draft.intern_constant(nested) == nested_id);
            expect(constant_value_equal(draft, nested.value, nested.value));
            const auto empty_type = array_type(draft, integer, 0u);
            const auto empty = draft.intern_constant({
                .type = empty_type,
                .value = ArrayConstant {.elements = {}},
            });
            const auto program = std::move(draft).finish();
            if (!expect(program.has_value())) {
                return;
            }
            expect(program->constants().constant(nested_id) == nested);
            expect(program->constants().constant(empty).type == empty_type);
        };

    "SemIR constants: array language equality recursively compares numeric values"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics);
            const auto number = draft.builtin_type(BuiltinType::F64);
            const auto positive =
                draft.intern_constant({.type = number, .value = F64Constant {.value = 0.0}});
            const auto negative =
                draft.intern_constant({.type = number, .value = F64Constant {.value = -0.0}});
            expect(positive != negative);
            const auto array = array_type(draft, number, 1u);
            const auto left = draft.intern_constant({
                .type = array,
                .value = ArrayConstant {.elements = {positive}},
            });
            const auto right = draft.intern_constant({
                .type = array,
                .value = ArrayConstant {.elements = {negative}},
            });
            expect(left != right);
            expect(constant_value_equal(
                draft,
                ArrayConstant {.elements = {left}},
                ArrayConstant {.elements = {right}}
            ));
            const auto not_a_number = draft.intern_constant({
                .type = number,
                .value = F64Constant {.value = std::numeric_limits<double>::quiet_NaN()},
            });
            expect(!(constant_value_equal(
                draft,
                ArrayConstant {.elements = {not_a_number}},
                ArrayConstant {.elements = {not_a_number}}
            )));
        };
});

} // namespace
