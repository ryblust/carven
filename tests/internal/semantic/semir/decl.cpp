module carven:test.internal.semantic.semir.decl;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.format;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.death;
import :test.harness.framework;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

namespace {

auto check_module_item_multiplicity(std::string_view scenario, std::size_t multiplicity) noexcept
    -> void {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder =
        begin_semir_test_compilation(sources, diagnostics, "semir.publication.module_item");
    const auto module_origin = make_semir_test_module_origin(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto structure = builder.reserve_struct_declaration();
    builder.define_declaration(
        structure,
        ConstructionStructDeclaration {
            .kind = RecordKind::Struct,
            .module_id = module_id,
            .name = builder.intern_spelling("Structure"),
            .origin = module_origin.origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {},
        }
    );
    const auto entries = std::vector<ModuleItem>(multiplicity, structure);
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = module_origin.provenance_module,
            .origin = module_origin.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = entries,
        }
    );
    builder.finish_declaration_heads();
    expect(expect_termination(scenario, [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

const TestSuite suite([] static noexcept {
    "SemIR publication invariant: every named declaration has a module item"_test =
        [] static noexcept {
            check_module_item_multiplicity("semir-publication-orphan-module-item", 0uz);
        };

    "SemIR publication invariant: every provenance module has one semantic module"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            const auto module_names = std::array<std::string_view, 2uz> {
                "semir.publication.provenance_first",
                "semir.publication.provenance_second",
            };
            auto builder = begin_semir_test_compilation_batch(sources, diagnostics, module_names);
            const auto module_origin = make_semir_test_module_origin(builder, 0uz);
            const auto first_module = builder.reserve_module_declaration();
            const auto second_module = builder.reserve_module_declaration();
            const auto declaration = ModuleDeclaration {
                .provenance_module = module_origin.provenance_module,
                .origin = module_origin.origin,
                .cpp_headers = {},
                .cpp_source_fragments = {},
                .items = {},
            };
            builder.define_declaration(first_module, declaration);
            builder.define_declaration(second_module, declaration);
            builder.finish_declaration_heads();
            expect(
                expect_termination("semir-publication-duplicate-provenance-module", [&] noexcept {
                    static_cast<void>(std::move(builder).finish());
                })
            );
        };

    "SemIR publication invariant: a named declaration has one module item"_test =
        [] static noexcept {
            check_module_item_multiplicity("semir-publication-duplicate-module-item", 2uz);
        };

    "SemIR publication invariant: a module item agrees with its declaration owner"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            const auto module_names = std::array<std::string_view, 2uz> {
                "semir.publication.item_owner_first",
                "semir.publication.item_owner_second",
            };
            auto builder = begin_semir_test_compilation_batch(sources, diagnostics, module_names);
            const auto first_module_origin = make_semir_test_module_origin(builder, 0uz);
            const auto second_module_origin = make_semir_test_module_origin(builder, 1uz);
            const auto first_module = builder.reserve_module_declaration();
            const auto second_module = builder.reserve_module_declaration();
            const auto structure = builder.reserve_struct_declaration();
            builder.define_declaration(
                structure,
                ConstructionStructDeclaration {
                    .kind = RecordKind::Struct,
                    .module_id = first_module,
                    .name = builder.intern_spelling("Structure"),
                    .origin = first_module_origin.origin,
                    .visibility = DeclarationVisibility::Module,
                    .fields = {},
                }
            );
            builder.define_declaration(
                first_module,
                ModuleDeclaration {
                    .provenance_module = first_module_origin.provenance_module,
                    .origin = first_module_origin.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {},
                }
            );
            builder.define_declaration(
                second_module,
                ModuleDeclaration {
                    .provenance_module = second_module_origin.provenance_module,
                    .origin = second_module_origin.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {structure},
                }
            );
            builder.finish_declaration_heads();
            expect(expect_termination("semir-publication-module-item-owner", [&] noexcept {
                static_cast<void>(std::move(builder).finish());
            }));
        };

    "SemIR declaration invariant: body ownership is unique and program-local"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                begin_semir_test_compilation(sources, diagnostics, "semir.declaration.body_owner");
            builder.finish_declaration_heads();
            const auto void_type = builder.builtin_type(BuiltinType::Void);
            const auto first =
                builder.append_body_callable(make_semir_test_callable_contract(builder, void_type));
            const auto second =
                builder.append_body_callable(make_semir_test_callable_contract(builder, void_type));
            const auto body = builder.reserve_body(BodyKind::Closure);
            builder.complete_callable(first, ClosureBodyImplementation {.body = body.id()});
            expect(expect_termination("semir-declaration-duplicate-body-owner", [&] noexcept {
                builder.complete_callable(second, ClosureBodyImplementation {.body = body.id()});
            }));

            auto foreign_sources = SourceManager();
            auto foreign_diagnostics = DiagnosticSink();
            auto foreign = begin_semir_test_compilation(
                foreign_sources,
                foreign_diagnostics,
                "semir.declaration.foreign_body"
            );
            foreign.finish_declaration_heads();
            const auto foreign_body = foreign.reserve_body(BodyKind::Closure);
            expect(expect_termination("semir-declaration-foreign-body-owner", [&] noexcept {
                builder.complete_callable(
                    second,
                    ClosureBodyImplementation {.body = foreign_body.id()}
                );
            }));
        };

    "SemIR publication invariant: external names require valid structured paths"_test =
        [] static noexcept {
            const auto paths = std::vector<std::vector<std::string>> {{}, {"native", "class"}};
            for (const auto& [index, components] : paths | std::views::enumerate) {
                auto sources = SourceManager();
                auto diagnostics = DiagnosticSink();
                auto builder = begin_semir_test_compilation(sources, diagnostics, "external");
                const auto module_origin = make_semir_test_module_origin(builder);
                const auto module_id = builder.reserve_module_declaration();
                builder.define_declaration(
                    module_id,
                    ModuleDeclaration {
                        .provenance_module = module_origin.provenance_module,
                        .origin = module_origin.origin,
                        .cpp_headers = {},
                        .cpp_source_fragments = {},
                        .items = {},
                    }
                );
                static_cast<void>(builder.intern_type(
                    {.value = CppTypeValue {
                         .form = CppNamedType {
                             .name =
                                 {.context_module = module_id,
                                  .lookup = CppNameLookup::Global,
                                  .components = components},
                             .arguments = {},
                         },
                     }}
                ));
                builder.finish_declaration_heads();
                expect(expect_termination(
                    std::format("semir-external-invalid-path-{}", index),
                    [&] noexcept { static_cast<void>(std::move(builder).finish()); }
                ));
            }
        };
});

} // namespace
