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
import :test.harness.framework;
import :test.internal.harness.death;
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

auto check_enum_case_multiplicity(std::string_view scenario, std::size_t multiplicity) noexcept
    -> void {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder =
        begin_semir_test_compilation(sources, diagnostics, "semir.publication.enum_case");
    const auto module_origin = make_semir_test_module_origin(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto enumeration = builder.reserve_enum_declaration();
    const auto enum_case = builder.reserve_enum_case_declaration();
    builder.define_declaration(
        enum_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = builder.intern_spelling("Case"),
            .origin = module_origin.origin,
            .payload_types = {},
            .constant = std::nullopt,
        }
    );
    const auto entries = std::vector<EnumCaseID>(multiplicity, enum_case);
    builder.define_declaration(
        enumeration,
        EnumDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("Enumeration"),
            .origin = module_origin.origin,
            .visibility = DeclarationVisibility::Module,
            .representation = PayloadEnumRepresentation {},
            .cases = entries,
            .supports_equality = true,
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = module_origin.provenance_module,
            .origin = module_origin.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {enumeration},
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

    "SemIR publication invariant: enum owner and case list are bidirectional"_test =
        [] static noexcept {
            check_enum_case_multiplicity("semir-publication-orphan-enum-case", 0uz);
        };

    "SemIR publication invariant: an enum case appears once in its owner list"_test =
        [] static noexcept {
            check_enum_case_multiplicity("semir-publication-duplicate-enum-case", 2uz);
        };

    "SemIR publication invariant: one callable belongs to one function"_test = [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto builder =
            begin_semir_test_compilation(sources, diagnostics, "semir.publication.callable");
        const auto module_origin = make_semir_test_module_origin(builder);
        const auto module_id = builder.reserve_module_declaration();
        const auto first = builder.reserve_function_declaration();
        const auto second = builder.reserve_function_declaration();
        const auto callable = builder.reserve_callable_declaration();
        const auto void_type = builder.builtin_type(BuiltinType::Void);
        builder.define_callable_contract(
            callable,
            make_semir_test_callable_contract(builder, void_type)
        );
        const auto function = [&](std::string_view name) noexcept {
            return FunctionDeclaration {
                .module_id = module_id,
                .name = builder.intern_spelling(name),
                .origin = module_origin.origin,
                .visibility = DeclarationVisibility::Module,
                .callable = callable,
                .entry_point = std::nullopt,
                .cpp_export_origin = std::nullopt,
                .is_const = false,
            };
        };
        builder.define_declaration(first, function("first"));
        builder.define_declaration(second, function("second"));
        builder.define_declaration(
            module_id,
            ModuleDeclaration {
                .provenance_module = module_origin.provenance_module,
                .origin = module_origin.origin,
                .cpp_headers = {},
                .cpp_source_fragments = {},
                .items = {first, second},
            }
        );
        builder.finish_declaration_heads();
        builder.complete_callable(
            callable,
            CppImportImplementation {.form_origin = module_origin.origin}
        );
        expect(expect_termination("semir-publication-duplicate-function-callable", [&] noexcept {
            static_cast<void>(std::move(builder).finish());
        }));
    };

    "SemIR publication invariant: a test has one owning module item"_test = [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto builder =
            begin_semir_test_compilation(sources, diagnostics, "semir.publication.test_item");
        const auto module_origin = make_semir_test_module_origin(builder);
        const auto module_id = builder.reserve_module_declaration();
        const auto test = builder.reserve_test();
        builder.define_declaration(
            module_id,
            ModuleDeclaration {
                .provenance_module = module_origin.provenance_module,
                .origin = module_origin.origin,
                .cpp_headers = {},
                .cpp_source_fragments = {},
                .items = {test, test},
            }
        );
        builder.finish_declaration_heads();
        auto reservation = builder.reserve_body(BodyKind::Test);
        const auto body_id = reservation.id();
        builder.define_test(
            test,
            TestDeclaration {
                .is_const = false,
                .module_id = module_id,
                .source =
                    {.label = builder.intern_spelling("test"), .origin = module_origin.origin},
                .body = body_id,
            }
        );
        auto graph = make_semir_test_body(std::move(reservation), module_origin.origin, builder);
        builder.add_body_draft(std::move(graph));
        expect(expect_termination("semir-publication-duplicate-test-item", [&] noexcept {
            static_cast<void>(std::move(builder).finish());
        }));
    };

    "SemIR publication invariant: function declarations use function bodies"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder = begin_semir_test_compilation(
                sources,
                diagnostics,
                "semir.publication.function_implementation"
            );
            const auto module_origin = make_semir_test_module_origin(builder);
            const auto module_id = builder.reserve_module_declaration();
            const auto function = builder.reserve_function_declaration();
            const auto callable = builder.reserve_callable_declaration();
            const auto void_type = builder.builtin_type(BuiltinType::Void);
            builder.define_callable_contract(
                callable,
                make_semir_test_callable_contract(builder, void_type)
            );
            builder.define_declaration(
                function,
                FunctionDeclaration {
                    .module_id = module_id,
                    .name = builder.intern_spelling("function"),
                    .origin = module_origin.origin,
                    .visibility = DeclarationVisibility::Module,
                    .callable = callable,
                    .entry_point = std::nullopt,
                    .cpp_export_origin = std::nullopt,
                    .is_const = false,
                }
            );
            builder.define_declaration(
                module_id,
                ModuleDeclaration {
                    .provenance_module = module_origin.provenance_module,
                    .origin = module_origin.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {function},
                }
            );
            builder.finish_declaration_heads();
            auto reservation = builder.reserve_body(BodyKind::Closure);
            const auto body_id = reservation.id();
            builder.complete_callable(callable, ClosureBodyImplementation {.body = body_id});
            auto graph =
                make_semir_test_body(std::move(reservation), module_origin.origin, builder);
            builder.add_body_draft(std::move(graph));
            expect(expect_termination("semir-publication-function-closure-body", [&] noexcept {
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

    "SemIR publication invariant: a closure callable has one closure operation"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder = begin_semir_test_compilation(
                sources,
                diagnostics,
                "semir.publication.closure_site"
            );
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
            builder.finish_declaration_heads();
            const auto void_type = builder.builtin_type(BuiltinType::Void);
            const auto callable =
                builder.append_body_callable(make_semir_test_callable_contract(builder, void_type));
            auto reservation = builder.reserve_body(BodyKind::Closure);
            const auto body_id = reservation.id();
            builder.complete_callable(callable, ClosureBodyImplementation {.body = body_id});
            auto graph =
                make_semir_test_body(std::move(reservation), module_origin.origin, builder);
            builder.add_body_draft(std::move(graph));
            expect(expect_termination("semir-publication-orphan-closure", [&] noexcept {
                static_cast<void>(std::move(builder).finish());
            }));
        };

    "SemIR declaration invariant: builder rejects cross-program module references"_test =
        [] static noexcept {
            auto first_sources = SourceManager();
            auto second_sources = SourceManager();
            auto first_diagnostics = DiagnosticSink();
            auto second_diagnostics = DiagnosticSink();
            auto first = begin_semir_test_compilation(
                first_sources,
                first_diagnostics,
                "semir.publication.first"
            );
            auto second = begin_semir_test_compilation(
                second_sources,
                second_diagnostics,
                "semir.publication.second"
            );
            const auto module_origin = make_semir_test_module_origin(first);
            const auto foreign_module = second.reserve_module_declaration();
            const auto structure = first.reserve_struct_declaration();
            expect(expect_termination("semir-publication-cross-program-module", [&] noexcept {
                first.define_declaration(
                    structure,
                    ConstructionStructDeclaration {
                        .kind = RecordKind::Struct,
                        .module_id = foreign_module,
                        .name = first.intern_spelling("Structure"),
                        .origin = module_origin.origin,
                        .visibility = DeclarationVisibility::Module,
                        .fields = {},
                    }
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
