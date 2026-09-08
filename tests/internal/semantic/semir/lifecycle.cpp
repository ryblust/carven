module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.lifecycle;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.catalog;
import :semantic.analysis.decl;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.analysis.validation;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.type;
import :semantic.visibility;
import :source.manager;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :test.internal.harness.death;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

struct BuiltProgram final {
    SemIRProgram program;
    TypeID boolean_type;
    TypeID text_type;
    TypeID primary_failure_type;
    TypeID guarded_failure_type;
    ConstantID text_constant;
    CallableID first_callable;
    CallableID second_callable;
    StructID holder_structure;
    StructID guarded_failure_structure;
};

auto build_program(std::string_view module_name) noexcept -> BuiltProgram {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("semir-fixture.cv", "");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path(module_name),
        },
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());

    auto diagnostics = DiagnosticSink();
    auto builder = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = builder.provenance_module_at(0uz);
    const auto program_source = builder.module_source(provenance_module);
    const auto origin = builder.append_source_origin(program_source, Span::at(0u));
    const auto first_function_name = builder.intern_spelling("native_a");
    const auto second_function_name = builder.intern_spelling("native_b");
    const auto text_spelling = builder.intern_spelling("value");
    const auto holder_name = builder.intern_spelling("Holder");
    const auto guarded_failure_name = builder.intern_spelling("GuardedFailure");
    const auto field_name = builder.intern_spelling("callback");

    const auto boolean = builder.intern_builtin_type(BuiltinType::Bool);
    CHECK(builder.intern_builtin_type(BuiltinType::Bool) == boolean);
    const auto text = builder.intern_builtin_type(BuiltinType::Str);
    const auto constant = builder.intern_constant(
        ConstantFact {
            .type = text,
            .value = StringConstant {.value = text_spelling},
        }
    );
    CHECK(
        builder.intern_constant(
            ConstantFact {
                .type = text,
                .value = StringConstant {.value = text_spelling},
            }
        )
        == constant
    );
    const auto no_failures = builder.empty_failure_set();
    const auto declared_no_failures = builder.add_empty_failure_term();
    CHECK(builder.empty_failure_set() == no_failures);

    const auto module_id = builder.reserve_module_declaration();
    const auto holder = builder.reserve_struct_declaration();
    const auto guarded_failure_structure = builder.reserve_struct_declaration();
    const auto nominal_holder = builder.intern_type(
        CanonicalType {
            .value = StructTypeValue {.structure = holder},
        }
    );
    const auto nominal_guarded_failure = builder.intern_type(
        CanonicalType {
            .value = StructTypeValue {.structure = guarded_failure_structure},
        }
    );
    CHECK(std::holds_alternative<StructTypeValue>(builder.type_copy(nominal_holder).value));
    const auto first_callable = builder.reserve_callable_declaration();
    const auto second_callable = builder.reserve_callable_declaration();
    const auto first_function = builder.reserve_function_declaration();
    const auto second_function = builder.reserve_function_declaration();
    const auto boolean_array = builder.append_construction_type(
        ConstructionType {
            .value = ConstructionArrayTypeValue {
                .element = boolean,
                .extent = 2u,
            },
        }
    );
    const auto contract = ConstructionCallableContract {
        .parameters =
            {
                ConstructionCallableParameter {
                    .access = AccessMode::Read,
                    .type = boolean,
                },
            },
        .result = boolean,
        .failures = declared_no_failures,
        .policy = FailureContractPolicy::Declared,
    };
    builder.define_callable_contract(first_callable, contract);
    builder.define_callable_contract(second_callable, contract);
    builder.define_declaration(
        holder,
        ConstructionStructDeclaration {
            .module_id = module_id,
            .name = holder_name,
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .fields =
                {
                    ConstructionStructField {
                        .name = field_name,
                        .type = boolean_array,
                        .origin = origin,
                    },
                },
            .capabilities = NominalCapabilities {.equality = false},
        }
    );
    builder.define_declaration(
        guarded_failure_structure,
        ConstructionStructDeclaration {
            .module_id = module_id,
            .name = guarded_failure_name,
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        first_function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = first_function_name,
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .callable = first_callable,
            .entry_point = std::nullopt,
            .cpp_export_origin = std::nullopt,
        }
    );
    builder.define_declaration(
        second_function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = second_function_name,
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .callable = second_callable,
            .entry_point = std::nullopt,
            .cpp_export_origin = std::nullopt,
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {holder, guarded_failure_structure, first_function, second_function},
        }
    );

    auto active_builder = ProgramDraft(std::move(builder));
    CHECK(expect_termination(
        std::format("compilation-builder-moved-finish-declarations-{}", module_name),
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the moved-from contract.
        [&] { builder.finish_declaration_heads(); }
    ));
    CHECK(expect_termination(
        std::format("compilation-builder-moved-identity-{}", module_name),
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the moved-from contract.
        [&] { static_cast<void>(builder.identity()); }
    ));
    active_builder.finish_declaration_heads();
    CHECK(expect_termination(
        std::format("compilation-builder-reserve-after-declarations-{}", module_name),
        [&] { static_cast<void>(active_builder.reserve_test()); }
    ));
    CHECK_EQ(active_builder.module_declaration_count(), 1uz);
    CHECK_EQ(active_builder.function_declaration_count(), 2uz);
    CHECK_EQ(active_builder.struct_declaration_count(), 2uz);
    CHECK_EQ(active_builder.callable_declaration_count(), 2uz);
    CHECK_EQ(active_builder.module_declaration_ids(), std::vector {module_id});
    CHECK_EQ(
        active_builder.function_declaration_ids(),
        std::vector {first_function, second_function}
    );
    CHECK_EQ(
        active_builder.struct_declaration_ids(),
        std::vector {holder, guarded_failure_structure}
    );
    CHECK_EQ(
        active_builder.callable_declaration_ids(),
        std::vector {first_callable, second_callable}
    );
    CHECK_EQ(
        active_builder.module_declaration_copy(module_id).provenance_module,
        provenance_module
    );
    CHECK_EQ(active_builder.function_declaration_copy(first_function).callable, first_callable);
    CHECK_EQ(
        active_builder.construction_struct_declaration_copy(holder).fields.front().type,
        ConstructionTypeRef(boolean_array)
    );
    CHECK_EQ(
        active_builder.construction_callable_contract_copy(first_callable).failures,
        declared_no_failures
    );

    active_builder.complete_callable(
        first_callable,
        CppImportImplementation {.form_origin = origin}
    );
    active_builder.complete_callable(
        second_callable,
        CppImportImplementation {.form_origin = origin}
    );
    const auto solved = active_builder.solve_construction();
    REQUIRE(solved.has_value());
    REQUIRE(diagnostics.empty());
    CHECK(expect_termination(std::format("solved-program-source-mutation-{}", module_name), [&] {
        static_cast<void>(active_builder.intern_spelling("late"));
    }));

    CHECK_EQ(active_builder.declarations().modules().size(), 1uz);
    CHECK_EQ(active_builder.declarations().callables().size(), 2uz);
    CHECK(active_builder.callable_crosses_cpp_boundary(first_callable));
    CHECK(active_builder.callable_crosses_cpp_boundary(second_callable));
    CHECK(
        active_builder.callable_signature(first_callable)
        == active_builder.callable_signature(second_callable)
    );

    auto solved_builder = ProgramDraft(std::move(active_builder));
    CHECK(expect_termination(
        std::format("compilation-builder-solved-source-query-{}", module_name),
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the moved-from contract.
        [&] { static_cast<void>(active_builder.types().type(boolean)); }
    ));
    auto program = std::move(solved_builder).seal();
    CHECK(expect_termination(
        std::format("compilation-builder-sealed-source-mutation-{}", module_name),
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the consumed-builder contract.
        [&] { static_cast<void>(solved_builder.intern_builtin_type(BuiltinType::Bool)); }
    ));
    CHECK(
        program.declarations().callable(first_callable).signature
        == program.declarations().callable(second_callable).signature
    );
    CHECK_EQ(program.callable_signatures().size(), 1uz);
    const auto& holder_declaration = program.declarations().structure(holder);
    REQUIRE_EQ(holder_declaration.fields.size(), 1uz);
    const auto& array_type = std::get<ArrayTypeValue>(
        program.types().type(holder_declaration.fields.front().type).value
    );
    CHECK_EQ(array_type.element, boolean);
    return BuiltProgram {
        .program = std::move(program),
        .boolean_type = boolean,
        .text_type = text,
        .primary_failure_type = nominal_holder,
        .guarded_failure_type = nominal_guarded_failure,
        .text_constant = constant,
        .first_callable = first_callable,
        .second_callable = second_callable,
        .holder_structure = holder,
        .guarded_failure_structure = guarded_failure_structure,
    };
}

auto require_callable_view_storage_rejected(bool use_enum) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("callable-view-storage.cv", "");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path(use_enum ? "semir.illegal_enum" : "semir.illegal_struct"),
        },
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());

    auto diagnostics = DiagnosticSink();
    auto builder = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = builder.provenance_module_at(0uz);
    const auto program_source = builder.module_source(provenance_module);
    const auto origin = builder.append_source_origin(program_source, Span::at(0u));
    const auto nominal_name = builder.intern_spelling(use_enum ? "Envelope" : "Holder");
    const auto member_name = builder.intern_spelling(use_enum ? "Payload" : "callback");
    const auto boolean = builder.intern_builtin_type(BuiltinType::Bool);
    const auto no_failures = builder.add_empty_failure_term();
    const auto callable_view = builder.append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = {},
                .result = boolean,
                .failures = no_failures,
            },
        }
    );
    const auto nested_array = builder.append_construction_type(
        ConstructionType {
            .value = ConstructionArrayTypeValue {
                .element = callable_view,
                .extent = 2u,
            },
        }
    );
    const auto module_id = builder.reserve_module_declaration();
    auto item = std::optional<ModuleItem>();
    if (use_enum) {
        const auto enumeration = builder.reserve_enum_declaration();
        const auto enum_case = builder.reserve_enum_case_declaration();
        builder.define_declaration(
            enum_case,
            ConstructionEnumCaseDeclaration {
                .owner = enumeration,
                .name = member_name,
                .origin = origin,
                .payload_types = {nested_array},
                .constant = std::nullopt,
            }
        );
        builder.define_declaration(
            enumeration,
            EnumDeclaration {
                .module_id = module_id,
                .name = nominal_name,
                .origin = origin,
                .visibility = DeclarationVisibility::Module,
                .representation = PayloadEnumRepresentation {},
                .cases = {enum_case},
                .capabilities = NominalCapabilities {.equality = false},
            }
        );
        item = ModuleItem {enumeration};
    } else {
        const auto structure = builder.reserve_struct_declaration();
        builder.define_declaration(
            structure,
            ConstructionStructDeclaration {
                .module_id = module_id,
                .name = nominal_name,
                .origin = origin,
                .visibility = DeclarationVisibility::Module,
                .fields =
                    {
                        ConstructionStructField {
                            .name = member_name,
                            .type = nested_array,
                            .origin = origin,
                        },
                    },
                .capabilities = NominalCapabilities {.equality = false},
            }
        );
        item = ModuleItem {structure};
    }
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {*item},
        }
    );
    builder.finish_declaration_heads();
    const auto solved = builder.solve_construction();
    REQUIRE(solved.has_value());

    const auto validated = validate_global_semantic_contracts(
        builder,
        compute_type_contents(builder.types(), builder.declarations())
    );
    CHECK_FALSE(validated.has_value());
    REQUIRE_EQ(diagnostics.size(), 1uz);
    CHECK_EQ(diagnostics.values().front().finding.code, DiagnosticCode::TypeCallableViewEscape);
}

} // namespace

static_assert(!std::default_initializable<ProgramIdentity>);
static_assert(!std::constructible_from<ProgramIdentity, std::uint64_t>);
static_assert(!std::default_initializable<TypeID>);
static_assert(std::constructible_from<MutableProgramTable<CanonicalType, TypeID>, ProgramIdentity>);
static_assert(std::ranges::range<IDTableEntries<TypeID, CanonicalType, ProgramIdentity>>);
static_assert(!std::copy_constructible<SemIRProgram>);
static_assert(std::movable<SemIRProgram>);
static_assert(!std::copy_constructible<SemIRBody>);

TEST_CASE(
    "Semantic program: solving finalizes signatures and publication delivers immutable stores"
) {
    const auto built = build_program("semir.lifecycle");
    CHECK(built.program.types().contains(built.boolean_type));
    CHECK(built.program.constants().contains(built.text_constant));
    CHECK(built.program.declarations().contains(built.first_callable));
    CHECK(built.program.declarations().contains(built.second_callable));
    CHECK(built.program.declarations().contains(built.holder_structure));
    auto function_count = 0uz;
    for (const auto [id, function] : built.program.declarations().functions()) {
        static_cast<void>(id);
        static_cast<void>(function);
        ++function_count;
    }
    CHECK_EQ(function_count, 2uz);
    CHECK_EQ(built.program.bodies().size(), 0uz);
    CHECK_EQ(built.program.tests().size(), 0uz);
}

TEST_CASE("SemIR identity: owner evidence rejects rows from another sealed program") {
    const auto first = build_program("semir.first");
    const auto second = build_program("semir.second");
    CHECK(first.program.identity() != second.program.identity());
    CHECK(first.boolean_type.owner() != second.boolean_type.owner());
    CHECK_FALSE(first.program.types().contains(second.boolean_type));
    CHECK_FALSE(first.program.constants().contains(second.text_constant));
    CHECK_FALSE(first.program.declarations().contains(second.first_callable));
}

TEST_CASE("SemIR lifecycle: moving the published owner consumes every immutable store") {
    auto built = build_program("semir.published_move");
    const auto boolean = built.boolean_type;
    const auto moved = SemIRProgram(std::move(built.program));
    CHECK(moved.types().contains(boolean));
    CHECK(expect_termination("semir-program-moved-identity", [&] {
        static_cast<void>(built.program.identity());
    }));
    CHECK(expect_termination("semir-program-moved-type-store", [&] {
        static_cast<void>(built.program.types().size());
    }));
}

TEST_CASE("SemIR global contracts: structure rejects nested callable-view storage") {
    require_callable_view_storage_rejected(false);
}

TEST_CASE("SemIR global contracts: enum payload rejects nested callable-view storage") {
    require_callable_view_storage_rejected(true);
}

TEST_CASE("SemIR lifecycle: pending function results cannot be read or solved") {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("pending.cv", "fn inferred() => 1;");
    REQUIRE(source.has_value());
    const auto inputs =
        std::array {CompilationModuleInput {.source_id = *source, .module_path = path("pending")}};
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    REQUIRE(catalog.has_value());
    auto usage = ImportUsage(catalog->view().imports().size());
    REQUIRE(resolve_declaration_heads(draft, catalog->view(), usage).has_value());
    const auto function = draft.function_declaration_ids().front();
    const auto callable = draft.function_declaration_copy(function).callable;
    REQUIRE(draft.pending_function_contract_copy(callable).has_value());
    CHECK(expect_termination("pending-function-contract-read", [&] {
        static_cast<void>(draft.construction_callable_contract_copy(callable));
    }));
    CHECK(expect_termination("pending-function-contract-solve", [&] {
        static_cast<void>(draft.solve_construction());
    }));
    REQUIRE(elaborate_body_batch(draft, catalog->view(), usage).has_value());
    CHECK_FALSE(draft.pending_function_contract_copy(callable).has_value());
    CHECK(expect_termination("duplicate-function-result-completion", [&] {
        draft.complete_function_result(callable, draft.intern_builtin_type(BuiltinType::I32));
    }));
    CHECK(draft.solve_construction().has_value());
}
