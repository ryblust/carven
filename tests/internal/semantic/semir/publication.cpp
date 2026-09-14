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
            .is_const = false,
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
