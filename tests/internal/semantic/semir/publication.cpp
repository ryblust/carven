module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.publication;

import :compiler.request;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto begin_compilation_batch(
    SourceManager& sources,
    DiagnosticSink& diagnostics,
    std::span<const std::string_view> module_names
) noexcept -> ProgramDraft {
    auto inputs = std::vector<CompilationModuleInput>();
    inputs.reserve(module_names.size());
    for (auto index = 0uz; index < module_names.size(); ++index) {
        const auto source =
            sources.append_virtual(std::format("semir-publication-{}.cv", index), "");
        REQUIRE(source.has_value());
        inputs.push_back(
            CompilationModuleInput {
                .source_id = *source,
                .module_path = path(module_names[index]),
            }
        );
    }
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

auto begin_compilation(
    SourceManager& sources,
    DiagnosticSink& diagnostics,
    std::string_view module_name
) noexcept -> ProgramDraft {
    const auto module_names = std::array {module_name};
    return begin_compilation_batch(sources, diagnostics, module_names);
}

struct ModuleFacts final {
    ProgramModuleID provenance_module;
    ProgramOriginID origin;
};

auto module_facts(ProgramDraft& builder, std::size_t index = 0uz) noexcept -> ModuleFacts {
    const auto module_id = builder.provenance_module_at(index);
    return {
        .provenance_module = module_id,
        .origin = builder.append_source_origin(builder.module_source(module_id), Span::at(0u)),
    };
}

auto callable_contract(ProgramDraft& builder, TypeID result) noexcept
    -> ConstructionCallableContract {
    return {
        .parameters = {},
        .result = result,
        .failures = builder.add_empty_failure_term(),
        .policy = FailureContractPolicy::Declared,
    };
}

auto minimal_body(
    BodyReservation reservation,
    ProgramOriginID origin,
    ProgramDraft& program
) noexcept -> StructuredBodyDraft {
    auto body = BodyBuilder(std::move(reservation), program);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
    return std::move(body).finish(
        SemanticRegion {
            .lifetime = lifetime,
            .origin = origin,
            .statements = {},
            .result = std::nullopt,
            .failures = BodyFailures(program.add_empty_failure_term()),
            .exits_test = false,
        }
    );
}

auto body_with_closure(
    BodyReservation reservation,
    ProgramOriginID origin,
    TypeID closure_type,
    CallableID closure_callable,
    ProgramDraft& program
) noexcept -> StructuredBodyDraft {
    auto body = BodyBuilder(std::move(reservation), program);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
    auto statements = std::vector<SemanticStatement>();
    statements.push_back(
        SemanticStatement {
            .origin = origin,
            .lifetime = lifetime,
            .value = SemExpressionStatement {
                .expression = body.make_expression(
                    closure_type,
                    lifetime,
                    origin,
                    SemClosure {.callable = closure_callable, .captures = {}}
                ),
            },
        }
    );
    return std::move(body).finish(
        SemanticRegion {
            .lifetime = lifetime,
            .origin = origin,
            .statements = std::move(statements),
            .result = std::nullopt,
            .failures = BodyFailures(program.add_empty_failure_term()),
            .exits_test = false,
        }
    );
}

auto publish(std::vector<StructuredBodyDraft> bodies, ProgramDraft& builder) noexcept -> void {
    for (auto& body : bodies) {
        builder.add_body_draft(std::move(body));
    }
}

auto publish(StructuredBodyDraft body, ProgramDraft& builder) noexcept -> void {
    auto bodies = std::vector<StructuredBodyDraft>();
    bodies.push_back(std::move(body));
    publish(std::move(bodies), builder);
}

} // namespace

TEST_CASE("SemIR publication: one closed topology owns every declaration case and body") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.complete");
    const auto facts = module_facts(builder);
    const auto void_type = builder.intern_builtin_type(BuiltinType::Void);
    const auto integer_type = builder.intern_builtin_type(BuiltinType::I32);
    const auto text_type = builder.intern_builtin_type(BuiltinType::Str);
    const auto text_value = builder.intern_spelling("publication");

    const auto module_id = builder.reserve_module_declaration();
    const auto function = builder.reserve_function_declaration();
    const auto structure = builder.reserve_struct_declaration();
    const auto enumeration = builder.reserve_enum_declaration();
    const auto enum_case = builder.reserve_enum_case_declaration();
    const auto module_constant = builder.reserve_module_constant_declaration();
    const auto function_callable = builder.reserve_callable_declaration();
    const auto test = builder.reserve_test();
    const auto constant = builder.intern_constant(
        ConstantFact {
            .type = text_type,
            .value = StringConstant {.value = text_value},
        }
    );

    builder.define_callable_contract(function_callable, callable_contract(builder, void_type));
    builder.define_declaration(
        function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("function"),
            .origin = facts.origin,
            .visibility = DeclarationVisibility::Module,
            .callable = function_callable,
            .entry_point = std::nullopt,
            .cpp_export_origin = std::nullopt,
        }
    );
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
        enum_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = builder.intern_spelling("Payload"),
            .origin = facts.origin,
            .payload_types = {integer_type},
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
            .cases = {enum_case},
            .capabilities = NominalCapabilities {.equality = true},
        }
    );
    builder.define_declaration(
        module_constant,
        ModuleConstantDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("constant"),
            .origin = facts.origin,
            .visibility = DeclarationVisibility::Module,
            .value = constant,
        }
    );
    builder.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = facts.provenance_module,
            .origin = facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {function, structure, enumeration, module_constant, test},
        }
    );
    builder.finish_declaration_heads();

    auto function_body = builder.reserve_body(BodyKind::Function);
    const auto function_body_id = function_body.id();
    builder.complete_callable(
        function_callable,
        FunctionBodyImplementation {.body = function_body_id}
    );

    const auto closure_callable =
        builder.append_body_callable(callable_contract(builder, void_type));
    auto closure_body = builder.reserve_body(BodyKind::Closure);
    const auto closure_body_id = closure_body.id();
    builder.complete_callable(
        closure_callable,
        ClosureBodyImplementation {.body = closure_body_id}
    );
    const auto closure_type = builder.intern_type(
        CanonicalType {
            .value = ClosureTypeValue {.callable = closure_callable},
        }
    );
    auto function_graph = body_with_closure(
        std::move(function_body),
        facts.origin,
        closure_type,
        closure_callable,
        builder
    );
    auto closure_graph = minimal_body(std::move(closure_body), facts.origin, builder);

    auto test_body = builder.reserve_body(BodyKind::Test);
    const auto test_body_id = test_body.id();
    builder.define_test(
        test,
        TestDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("publication topology"),
            .origin = facts.origin,
            .body = test_body_id,
        }
    );
    auto test_graph = minimal_body(std::move(test_body), facts.origin, builder);

    auto graphs = std::vector<StructuredBodyDraft>();
    graphs.push_back(std::move(closure_graph));
    graphs.push_back(std::move(function_graph));
    graphs.push_back(std::move(test_graph));
    publish(std::move(graphs), builder);
    auto finished = std::move(builder).finish();
    REQUIRE(finished.has_value());
    const auto program = std::move(*finished);

    const auto& published_module = program.declarations().module_decl(module_id);
    CHECK_EQ(published_module.items.size(), 5uz);
    CHECK(std::ranges::contains(published_module.items, ModuleItem {function}));
    CHECK(std::ranges::contains(published_module.items, ModuleItem {structure}));
    CHECK(std::ranges::contains(published_module.items, ModuleItem {enumeration}));
    CHECK(std::ranges::contains(published_module.items, ModuleItem {module_constant}));
    CHECK(std::ranges::contains(published_module.items, ModuleItem {test}));
    CHECK_EQ(program.declarations().enumeration(enumeration).cases, std::vector {enum_case});
    CHECK_EQ(program.declarations().enum_case(enum_case).owner, enumeration);
    CHECK_EQ(program.declarations().body_for_callable(function_callable), function_body_id);
    CHECK_EQ(program.declarations().body_for_callable(closure_callable), closure_body_id);
    CHECK_EQ(program.declarations().callable_for_body(function_body_id), function_callable);
    CHECK_EQ(program.declarations().callable_for_body(closure_body_id), closure_callable);
    CHECK_FALSE(program.declarations().callable_for_body(test_body_id).has_value());
    CHECK_EQ(program.tests().test(test).body, test_body_id);
    CHECK(diagnostics.empty());
}

TEST_CASE("SemIR publication: semantic module order is independent of provenance order") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    const auto module_names = std::array<std::string_view, 2uz> {
        "semir.publication.order_first",
        "semir.publication.order_second",
    };
    auto builder = begin_compilation_batch(sources, diagnostics, module_names);
    const auto first_facts = module_facts(builder, 0uz);
    const auto second_facts = module_facts(builder, 1uz);
    const auto first_module = builder.reserve_module_declaration();
    const auto second_module = builder.reserve_module_declaration();
    builder.define_declaration(
        first_module,
        ModuleDeclaration {
            .provenance_module = second_facts.provenance_module,
            .origin = second_facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        }
    );
    builder.define_declaration(
        second_module,
        ModuleDeclaration {
            .provenance_module = first_facts.provenance_module,
            .origin = first_facts.origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        }
    );
    builder.finish_declaration_heads();
    auto finished = std::move(builder).finish();
    REQUIRE(finished.has_value());
    const auto program = std::move(*finished);
    CHECK_EQ(
        program.declarations().module_decl(first_module).provenance_module,
        second_facts.provenance_module
    );
    CHECK_EQ(
        program.declarations().module_decl(second_module).provenance_module,
        first_facts.provenance_module
    );
    CHECK(diagnostics.empty());
}

TEST_CASE("SemIR publication invariant: every failure-set member is nominal") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.failure_member");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto boolean = builder.intern_builtin_type(BuiltinType::Bool);
    static_cast<void>(builder.intern_failure_set({boolean}));
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
    CHECK(expect_termination("semir-publication-nonnominal-failure", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

TEST_CASE("SemIR publication invariant: every constant matches its canonical type") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.constant_fact");
    const auto facts = module_facts(builder);
    const auto module_id = builder.reserve_module_declaration();
    const auto integer = builder.intern_builtin_type(BuiltinType::I32);
    static_cast<void>(builder.intern_constant(
        ConstantFact {
            .type = integer,
            .value = BooleanConstant {.value = true},
        }
    ));
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
    CHECK(expect_termination("semir-publication-constant-type", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

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
    const auto void_type = builder.intern_builtin_type(BuiltinType::Void);
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
    const auto void_type = builder.intern_builtin_type(BuiltinType::Void);
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
    const auto void_type = builder.intern_builtin_type(BuiltinType::Void);
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
    const auto void_type = builder.intern_builtin_type(BuiltinType::Void);
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
    for (const auto& components : paths) {
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
        CHECK(expect_termination("semir-external-invalid-path", [&] noexcept {
            static_cast<void>(std::move(builder).finish());
        }));
    }
}
