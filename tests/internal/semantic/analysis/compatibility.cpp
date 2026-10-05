module carven:test.internal.semantic.analysis.compatibility;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto path() noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value("compatibility");
    require(result.has_value());
    return std::move(*result);
}

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("compatibility.cv", "");
    require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = path(),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

auto callable_contract(TypeID result, FailureTermID failures) noexcept
    -> ConstructionCallableContract {
    return {
        .parameters = {},
        .result = result,
        .failures = failures,
        .policy = FailureContractPolicy::Inferred,
    };
}

const TestSuite suite([] static noexcept {
    "Semantic type compatibility: owning callables differ from structural views"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto compilation = begin_compilation(sources, diagnostics);

            expect(builtin_type_supports_equality(BuiltinType::Bool));
            expect(!(builtin_type_supports_equality(BuiltinType::Void)));

            const auto boolean = compilation.builtin_type(BuiltinType::Bool);
            const auto integer = compilation.builtin_type(BuiltinType::I32);
            const auto empty_failures = compilation.add_empty_failure_term();
            const auto provenance_module = compilation.provenance_module_at(0uz);
            const auto origin = compilation.append_source_origin(
                compilation.module_source(provenance_module),
                Span::at(0u)
            );
            const auto module_id = compilation.reserve_module_declaration();
            const auto failure_structure = compilation.reserve_struct_declaration();
            const auto failure_type = compilation.intern_type(
                CanonicalType {
                    .value = StructTypeValue {.structure = failure_structure},
                }
            );
            const auto widened_failures = compilation.add_concrete_failure_term({failure_type});
            const auto view = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionCallableViewTypeValue {
                        .parameters = {},
                        .result = integer,
                        .failures = empty_failures,
                    },
                }
            );
            const auto widened_view = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionCallableViewTypeValue {
                        .parameters = {},
                        .result = integer,
                        .failures = widened_failures,
                    },
                }
            );

            const auto function_callable = compilation.reserve_callable_declaration();
            const auto staged_callable = compilation.reserve_callable_declaration();
            const auto first_closure_callable = compilation.reserve_callable_declaration();
            const auto second_closure_callable = compilation.reserve_callable_declaration();
            compilation.define_callable_contract(
                function_callable,
                callable_contract(integer, empty_failures)
            );
            compilation.define_callable_contract(
                staged_callable,
                ConstructionCallableContract {
                    .parameters =
                        {{.stage = ParameterStage::Static,
                          .access = AccessMode::Read,
                          .type = integer}},
                    .result = integer,
                    .failures = empty_failures,
                    .policy = FailureContractPolicy::Inferred,
                }
            );
            compilation.define_callable_contract(
                first_closure_callable,
                callable_contract(integer, empty_failures)
            );
            compilation.define_callable_contract(
                second_closure_callable,
                callable_contract(integer, empty_failures)
            );
            compilation.define_declaration(
                failure_structure,
                ConstructionStructDeclaration {
                    .kind = RecordKind::Struct,
                    .module_id = module_id,
                    .name = compilation.intern_spelling("Failure"),
                    .origin = origin,
                    .visibility = DeclarationVisibility::Module,
                    .fields = {},
                }
            );
            compilation.define_declaration(
                module_id,
                ModuleDeclaration {
                    .provenance_module = provenance_module,
                    .origin = origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {failure_structure},
                }
            );
            compilation.finish_declaration_heads();
            const auto function = compilation.intern_type(
                CanonicalType {
                    .value = FunctionTypeValue {.callable = function_callable},
                }
            );
            const auto staged_function = compilation.intern_type(
                CanonicalType {
                    .value = FunctionTypeValue {.callable = staged_callable},
                }
            );
            const auto ordinary_parameter_view = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionCallableViewTypeValue {
                        .parameters =
                            {{.stage = ParameterStage::Runtime,
                              .access = AccessMode::Read,
                              .type = integer}},
                        .result = integer,
                        .failures = empty_failures,
                    },
                }
            );
            const auto first_closure = compilation.intern_type(
                CanonicalType {
                    .value = ClosureTypeValue {.callable = first_closure_callable},
                }
            );
            const auto second_closure = compilation.intern_type(
                CanonicalType {
                    .value = ClosureTypeValue {.callable = second_closure_callable},
                }
            );
            const auto nested_view = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionArrayTypeValue {
                        .element = view,
                        .extent = 2u,
                    },
                }
            );
            const auto nested_widened_view = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionArrayTypeValue {
                        .element = widened_view,
                        .extent = 2u,
                    },
                }
            );
            const auto different_extent = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionArrayTypeValue {
                        .element = view,
                        .extent = 3u,
                    },
                }
            );
            const auto canonical_integer_array = compilation.intern_type(
                CanonicalType {
                    .value = ArrayTypeValue {
                        .element = integer,
                        .extent = 2u,
                    },
                }
            );
            const auto construction_integer_array = compilation.append_construction_type(
                ConstructionType {
                    .value = ConstructionArrayTypeValue {
                        .element = integer,
                        .extent = 2u,
                    },
                }
            );

            expect(!(type_shapes_compatible(compilation, boolean, integer)));
            expect(type_shapes_compatible(compilation, view, widened_view));
            expect(type_shapes_compatible(compilation, function, view));
            expect(!type_shapes_compatible(compilation, staged_function, ordinary_parameter_view));
            expect(type_shapes_compatible(compilation, first_closure, view));
            expect(!(type_shapes_compatible(compilation, first_closure, second_closure)));
            expect(type_shapes_compatible(compilation, nested_view, nested_widened_view));
            expect(!(type_shapes_compatible(compilation, nested_view, different_extent)));
            expect(type_shapes_compatible(
                compilation,
                canonical_integer_array,
                construction_integer_array
            ));

            expect(!(type_contains_callable_view(compilation, integer)));
            expect(!(type_contains_callable_view(compilation, function)));
            expect(!(type_contains_callable_view(compilation, first_closure)));
            expect(type_contains_callable_view(compilation, view));
            expect(type_contains_callable_view(compilation, nested_view));
            expect(diagnostics.empty());
        };
});

} // namespace
