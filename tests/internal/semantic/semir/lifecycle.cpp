module carven:test.internal.semantic.semir.lifecycle;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    require(result.has_value());
    return std::move(*result);
}

struct BuiltProgram final {
    SemIRProgram program;
    TypeID boolean_type;
    ConstantID text_constant;
    CallableID first_callable;
    CallableID second_callable;
    StructID holder_structure;
};

auto build_program(std::string_view module_name) noexcept -> BuiltProgram {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("semir-fixture.cv", "");
    require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = path(module_name),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value());

    auto diagnostics = DiagnosticSink();
    auto builder = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = builder.provenance_module_at(0uz);
    const auto program_source = builder.module_source(provenance_module);
    const auto origin = builder.append_source_origin(program_source, Span::at(0u));
    const auto first_function_name = builder.intern_spelling("native_a");
    const auto second_function_name = builder.intern_spelling("native_b");
    const auto text_spelling = builder.intern_spelling("value");
    const auto holder_name = builder.intern_spelling("Holder");
    const auto field_name = builder.intern_spelling("values");

    const auto boolean = builder.builtin_type(BuiltinType::Bool);
    expect(builder.builtin_type(BuiltinType::Bool) == boolean);
    const auto text = builder.builtin_type(BuiltinType::Str);
    const auto constant = builder.intern_constant(
        ConstantFact {
            .type = text,
            .value = TextConstant {.value = text_spelling},
        }
    );
    expect(
        builder.intern_constant(
            ConstantFact {
                .type = text,
                .value = TextConstant {.value = text_spelling},
            }
        )
        == constant
    );
    const auto no_failures = builder.empty_failure_set();
    expect(builder.empty_failure_set() == no_failures);

    const auto module_id = builder.reserve_module_declaration();
    const auto holder = builder.reserve_struct_declaration();
    const auto nominal_holder = builder.intern_type(
        CanonicalType {
            .value = StructTypeValue {.structure = holder},
        }
    );
    expect(std::holds_alternative<StructTypeValue>(builder.type_copy(nominal_holder).value));
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
        .execution = CallableExecutionKind::Synchronous,
        .parameters =
            {
                ConstructionCallableParameter {
                    .stage = ParameterStage::Runtime,
                    .access = AccessMode::Read,
                    .type = boolean,
                },
            },
        .result = boolean,
        .failures = no_failures,
        .policy = FailureContractPolicy::Declared,
    };
    builder.define_callable_contract(first_callable, contract);
    builder.define_callable_contract(second_callable, contract);
    builder.define_declaration(
        holder,
        ConstructionStructDeclaration {
            .kind = RecordKind::Struct,
            .module_id = module_id,
            .name = holder_name,
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {
                ConstructionStructField {
                    .name = field_name,
                    .type = boolean_array,
                    .origin = origin,
                },
            },
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
            .is_const = false,
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
            .is_const = false,
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {holder, first_function, second_function},
        }
    );

    auto active_builder = ProgramDraft(std::move(builder));
    active_builder.finish_declaration_heads();
    expect(((active_builder.module_declaration_copy(module_id).provenance_module)
            == (provenance_module)))
        .note(
            "active_builder.module_declaration_copy(module_id).provenance_module == provenance_module"
        );
    expect(((active_builder.function_declaration_copy(first_function).callable)
            == (first_callable)))
        .note(
            "active_builder.function_declaration_copy(first_function).callable == first_callable"
        );
    expect(((active_builder.construction_struct_declaration_copy(holder).fields.front().type)
            == (ConstructionTypeRef(boolean_array))))
        .note(
            "active_builder.construction_struct_declaration_copy(holder).fields.front().type == ConstructionTypeRef(boolean_array)"
        );
    expect(((active_builder.construction_callable_contract_copy(first_callable).failures)
            == (ConstructionFailureRef(no_failures))))
        .note(
            "active_builder.construction_callable_contract_copy(first_callable).failures == ConstructionFailureRef(no_failures)"
        );

    active_builder.complete_callable(
        first_callable,
        CppImportImplementation {
            .form_origin = origin,
            .target = {
                .context_module = module_id,
                .lookup = CppNameLookup::Global,
                .components = {"first"}
            },
        }
    );
    active_builder.complete_callable(
        second_callable,
        CppImportImplementation {
            .form_origin = origin,
            .target = {
                .context_module = module_id,
                .lookup = CppNameLookup::Global,
                .components = {"second"}
            },
        }
    );
    auto finished = std::move(active_builder).finish();
    require(finished.has_value());
    require(diagnostics.empty());
    auto program = std::move(*finished);
    expect(
        program.declarations().callable(first_callable).signature
        == program.declarations().callable(second_callable).signature
    );
    expect_equal(program.callable_signatures().size(), 1uz);
    const auto& holder_declaration = program.declarations().structure(holder);
    require_equal(holder_declaration.fields.size(), 1uz);
    const auto& array_type = std::get<ArrayTypeValue>(
        program.types().type(holder_declaration.fields.front().type).value
    );
    expect(((array_type.element) == (boolean))).note("array_type.element == boolean");
    return BuiltProgram {
        .program = std::move(program),
        .boolean_type = boolean,
        .text_constant = constant,
        .first_callable = first_callable,
        .second_callable = second_callable,
        .holder_structure = holder,
    };
}

} // namespace

static_assert(!std::default_initializable<ProgramIdentity>);
static_assert(!std::constructible_from<ProgramIdentity, std::uint64_t>);
static_assert(!std::default_initializable<TypeID>);
static_assert(std::constructible_from<MutableProgramTable<CanonicalType, TypeID>, ProgramIdentity>);
static_assert(std::ranges::range<IDTableEntries<TypeID, CanonicalType, ProgramIdentity>>);
static_assert(!std::copy_constructible<SemIRProgram>);
static_assert(std::move_constructible<SemIRProgram>);
static_assert(!std::is_move_assignable_v<SemIRProgram>);
static_assert(!std::copy_constructible<SemIRBody>);

namespace {

const TestSuite suite([] static noexcept {
    "Semantic program: solving finalizes signatures and publication delivers immutable stores"_test =
        [] static noexcept {
            const auto built = build_program("semir.lifecycle");
            expect(built.program.types().contains(built.boolean_type));
            expect(built.program.constants().contains(built.text_constant));
            expect(built.program.declarations().contains(built.first_callable));
            expect(built.program.declarations().contains(built.second_callable));
            expect(built.program.declarations().contains(built.holder_structure));
            auto function_count = 0uz;
            for (const auto [id, function] : built.program.declarations().functions()) {
                static_cast<void>(id);
                static_cast<void>(function);
                ++function_count;
            }
            expect_equal(function_count, 2uz);
            expect_equal(built.program.bodies().size(), 0uz);
            expect_equal(built.program.tests().size(), 0uz);
        };

    "SemIR identity: independent programs retain distinct identities"_test = [] static noexcept {
        const auto first = build_program("semir.first");
        const auto second = build_program("semir.second");
        expect(first.program.identity() != second.program.identity());
        expect(first.boolean_type.owner() != second.boolean_type.owner());
        expect(!(first.program.types().contains(second.boolean_type)));
        expect(!(first.program.constants().contains(second.text_constant)));
        expect(!(first.program.declarations().contains(second.first_callable)));
    };
});

} // namespace
