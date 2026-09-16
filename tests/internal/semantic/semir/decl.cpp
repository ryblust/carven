module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.decl;

import :compiler.request;
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
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

using namespace semir_test;

TEST_CASE("SemIR publication invariant: every named declaration has a module item") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.missing_item");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto structure = builder.reserve_struct_declaration();
    builder.define_declaration(
        structure,
        ConstructionStructDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("Structure"),
            .origin = facts.origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        }
    );
    builder.finish_declaration_heads();
    CHECK(expect_termination("semir-publication-orphan-module-item", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: every provenance module has one semantic module") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    const auto module_names = std::array<std::string_view, 2uz> {
        "semir.publication.provenance_first",
        "semir.publication.provenance_second",
    };
    auto builder = begin_compilation_batch(sources, diagnostics, module_names);
    const auto facts = module_facts(builder, 0uz);
    const auto first_module = builder.reserve_module_declaration();
    const auto second_module = builder.reserve_module_declaration();
    const auto declaration = ModuleDeclaration {
        .provenance_module = facts.provenance_module,
        .origin = facts.origin,
        .cpp_headers = {},
        .cpp_source_fragments = {},
        .items = {},
    };
    builder.define_declaration(first_module, declaration);
    builder.define_declaration(second_module, declaration);
    builder.finish_declaration_heads();
    CHECK(expect_termination("semir-publication-duplicate-provenance-module", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: a named declaration has one module item") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.module_item");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto structure = builder.reserve_struct_declaration();
    builder.define_declaration(
        structure,
        ConstructionStructDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("Structure"),
            .origin = facts.origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {structure, structure},
        }
    );
    builder.finish_declaration_heads();
    CHECK(expect_termination("semir-publication-duplicate-module-item", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: a module item agrees with its declaration owner") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    const auto module_names = std::array<std::string_view, 2uz> {
        "semir.publication.item_owner_first",
        "semir.publication.item_owner_second",
    };
    auto builder = begin_compilation_batch(sources, diagnostics, module_names);
    const auto first_facts = module_facts(builder, 0uz);
    const auto second_facts = module_facts(builder, 1uz);
    const auto first_module = builder.reserve_module_declaration();
    const auto second_module = builder.reserve_module_declaration();
    const auto structure = builder.reserve_struct_declaration();
    builder.define_declaration(
        structure,
        ConstructionStructDeclaration {
            .module_id = first_module,
            .name = builder.intern_spelling("Structure"),
            .origin = first_facts.origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        first_module,
        ModuleDeclaration {
            .provenance_module = first_facts.provenance_module,
            .origin = first_facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        }
    );
    builder.define_declaration(
        second_module,
        ModuleDeclaration {
            .provenance_module = second_facts.provenance_module,
            .origin = second_facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {structure},
        }
    );
    builder.finish_declaration_heads();
    CHECK(expect_termination("semir-publication-module-item-owner", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: enum owner and case list are bidirectional") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.enum_case");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto enumeration = builder.reserve_enum_declaration();
    const auto enum_case = builder.reserve_enum_case_declaration();
    builder.define_declaration(
        enum_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = builder.intern_spelling("Case"),
            .origin = facts.origin,
            .payload_types = {},
            .constant = std::nullopt,
        }
    );
    builder.define_declaration(
        enumeration,
        EnumDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("Enumeration"),
            .origin = facts.origin,
            .visibility = DeclarationVisibility::Module,
            .representation = PayloadEnumRepresentation {},
            .cases = {},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {enumeration},
        }
    );
    builder.finish_declaration_heads();
    CHECK(expect_termination("semir-publication-orphan-enum-case", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: an enum case appears once in its owner list") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.enum_unique");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto enumeration = builder.reserve_enum_declaration();
    const auto enum_case = builder.reserve_enum_case_declaration();
    builder.define_declaration(
        enum_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = builder.intern_spelling("Case"),
            .origin = facts.origin,
            .payload_types = {},
            .constant = std::nullopt,
        }
    );
    builder.define_declaration(
        enumeration,
        EnumDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("Enumeration"),
            .origin = facts.origin,
            .visibility = DeclarationVisibility::Module,
            .representation = PayloadEnumRepresentation {},
            .cases = {enum_case, enum_case},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {enumeration},
        }
    );
    builder.finish_declaration_heads();
    CHECK(expect_termination("semir-publication-duplicate-enum-case", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: one callable belongs to one function") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.callable");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto first = builder.reserve_function_declaration();
    const auto second = builder.reserve_function_declaration();
    const auto callable = builder.reserve_callable_declaration();
    const auto void_type = builder.builtin_type(BuiltinType::Void);
    builder.define_callable_contract(callable, callable_contract(builder, void_type));
    const auto function = [&](std::string_view name) noexcept {
        return FunctionDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling(name),
            .origin = facts.origin,
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
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {first, second},
        }
    );
    builder.finish_declaration_heads();
    builder.complete_callable(callable, CppImportImplementation {.form_origin = facts.origin});
    CHECK(expect_termination("semir-publication-duplicate-function-callable", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: a test has one owning module item") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.test_item");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto test = builder.reserve_test();
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
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
            .name = builder.intern_spelling("test"),
            .origin = facts.origin,
            .body = body_id,
        }
    );
    auto graph = minimal_body(std::move(reservation), facts.origin, builder);
    publish(std::move(graph), builder);
    CHECK(expect_termination("semir-publication-duplicate-test-item", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: function declarations use function bodies") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder =
        begin_compilation(sources, diagnostics, "semir.publication.function_implementation");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto function = builder.reserve_function_declaration();
    const auto callable = builder.reserve_callable_declaration();
    const auto void_type = builder.builtin_type(BuiltinType::Void);
    builder.define_callable_contract(callable, callable_contract(builder, void_type));
    builder.define_declaration(
        function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("function"),
            .origin = facts.origin,
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
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {function},
        }
    );
    builder.finish_declaration_heads();
    auto reservation = builder.reserve_body(BodyKind::Closure);
    const auto body_id = reservation.id();
    builder.complete_callable(callable, ClosureBodyImplementation {.body = body_id});
    auto graph = minimal_body(std::move(reservation), facts.origin, builder);
    publish(std::move(graph), builder);
    CHECK(expect_termination("semir-publication-function-closure-body", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR declaration invariant: body ownership is unique and program-local") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.declaration.body_owner");
    builder.finish_declaration_heads();
    const auto void_type = builder.builtin_type(BuiltinType::Void);
    const auto first = builder.append_body_callable(callable_contract(builder, void_type));
    const auto second = builder.append_body_callable(callable_contract(builder, void_type));
    const auto body = builder.reserve_body(BodyKind::Closure);
    builder.complete_callable(first, ClosureBodyImplementation {.body = body.id()});
    CHECK(expect_termination("semir-declaration-duplicate-body-owner", [&] noexcept {
        builder.complete_callable(second, ClosureBodyImplementation {.body = body.id()});
    }));

    auto foreign_sources = SourceManager();
    auto foreign_diagnostics = DiagnosticSink();
    auto foreign =
        begin_compilation(foreign_sources, foreign_diagnostics, "semir.declaration.foreign_body");
    foreign.finish_declaration_heads();
    const auto foreign_body = foreign.reserve_body(BodyKind::Closure);
    CHECK(expect_termination("semir-declaration-foreign-body-owner", [&] noexcept {
        builder.complete_callable(second, ClosureBodyImplementation {.body = foreign_body.id()});
    }));
}

TEST_CASE("SemIR publication invariant: a closure callable has one closure operation") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.closure_site");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        }
    );
    builder.finish_declaration_heads();
    const auto void_type = builder.builtin_type(BuiltinType::Void);
    const auto callable = builder.append_body_callable(callable_contract(builder, void_type));
    auto reservation = builder.reserve_body(BodyKind::Closure);
    const auto body_id = reservation.id();
    builder.complete_callable(callable, ClosureBodyImplementation {.body = body_id});
    auto graph = minimal_body(std::move(reservation), facts.origin, builder);
    publish(std::move(graph), builder);
    CHECK(expect_termination("semir-publication-orphan-closure", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR declaration invariant: builder rejects cross-program module references") {
    auto first_sources = SourceManager();
    auto second_sources = SourceManager();
    auto first_diagnostics = DiagnosticSink();
    auto second_diagnostics = DiagnosticSink();
    auto first = begin_compilation(first_sources, first_diagnostics, "semir.publication.first");
    auto second = begin_compilation(second_sources, second_diagnostics, "semir.publication.second");
    const auto facts = module_facts(first);
    const auto foreign_module = second.reserve_module_declaration();
    const auto structure = first.reserve_struct_declaration();
    CHECK(expect_termination("semir-publication-cross-program-module", [&] noexcept {
        first.define_declaration(
            structure,
            ConstructionStructDeclaration {
                .module_id = foreign_module,
                .name = first.intern_spelling("Structure"),
                .origin = facts.origin,
                .visibility = DeclarationVisibility::Module,
                .fields = {},
                .capabilities = NominalCapabilities {.equality = true},
            }
        );
    }));
}

TEST_CASE("SemIR publication invariant: external names require valid structured paths") {
    const auto paths = std::vector<std::vector<std::string>> {{}, {"native", "class"}};
    for (const auto& [index, components] : paths | std::views::enumerate) {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto builder = begin_compilation(sources, diagnostics, "external");
        const auto facts = module_facts(builder);
        const auto module_id = builder.reserve_module_declaration();
        builder.define_declaration(
            module_id,
            ModuleDeclaration {
                .provenance_module = facts.provenance_module,
                .origin = facts.origin,
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
        CHECK(
            expect_termination(std::format("semir-external-invalid-path-{}", index), [&] noexcept {
                static_cast<void>(std::move(builder).finish());
            })
        );
    }
}
