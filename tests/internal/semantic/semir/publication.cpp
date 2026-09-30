module carven:test.internal.semantic.semir.publication;

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
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.stage;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

using namespace semir_test;

namespace {

namespace ct = carven::testing;

// A residual region that still calls a function with static parameters names
// no instance.
auto check_unresolved_residual_call() noexcept -> void {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto builder = begin_compilation(sources, diagnostics, "semir.publication.residual_call");
    const auto facts = module_facts(builder);
    const auto boolean = builder.builtin_type(BuiltinType::Bool);
    const auto void_type = builder.builtin_type(BuiltinType::Void);
    const auto module_id = builder.reserve_module_declaration();
    const auto function = builder.reserve_function_declaration();
    const auto callable = builder.reserve_callable_declaration();
    const auto test = builder.reserve_test();
    auto signature = callable_contract(builder, void_type);
    signature.parameters = {
        {.stage = ParameterStage::Static, .access = AccessMode::Read, .type = boolean},
        {.stage = ParameterStage::Runtime, .access = AccessMode::Read, .type = boolean},
    };
    builder.define_callable_contract(callable, std::move(signature));
    builder.define_declaration(
        function,
        FunctionDeclaration {
            .module_id = module_id,
            .name = builder.intern_spelling("staged"),
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
            .items = {function, test},
        }
    );
    builder.finish_declaration_heads();
    auto reservation = builder.reserve_body(BodyKind::Function);
    builder.complete_callable(callable, FunctionBodyImplementation {.body = reservation.id()});
    auto function_body = BodyBuilder(std::move(reservation), builder);
    const auto function_lifetime =
        function_body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, facts.origin);
    for (const auto name : {"static_value", "runtime_value"}) {
        static_cast<void>(function_body.add_parameter(
            builder.intern_spelling(name),
            boolean,
            function_lifetime,
            AccessMode::Read,
            facts.origin
        ));
    }
    auto function_graph = std::move(function_body)
                              .finish(
                                  SemanticRegion {
                                      .lifetime = function_lifetime,
                                      .origin = facts.origin,
                                      .statements = {},
                                      .result = std::nullopt,
                                      .result_reachable = false,
                                      .failures = BodyFailures(builder.add_empty_failure_term()),
                                      .exits_test = false,
                                  }
                              );
    const auto constant =
        builder.intern_constant({.type = boolean, .value = BooleanConstant {.value = true}});
    publish(std::move(function_graph), builder);

    auto caller_reservation = builder.reserve_body(BodyKind::Test);
    builder.define_test(
        test,
        TestDeclaration {
            .is_const = false,
            .module_id = module_id,
            .source = {.label = builder.intern_spelling("residual call"), .origin = facts.origin},
            .body = caller_reservation.id(),
        }
    );
    auto caller = BodyBuilder(std::move(caller_reservation), builder);
    const auto lifetime =
        caller.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, facts.origin);
    const auto function_type =
        builder.intern_type({.value = FunctionTypeValue {.callable = callable}});
    const auto argument =
        caller.make_expression(boolean, lifetime, facts.origin, SemConstant {.constant = constant});
    auto expression = caller.make_expression(
        void_type,
        lifetime,
        facts.origin,
        SemCall {
            .callee = OwnedSemanticExpression(caller.make_expression(
                function_type,
                lifetime,
                facts.origin,
                SemCallable {.callable = callable}
            )),
            .target = callable,
            .arguments =
                {
                    {.access = AccessMode::Read, .expression = argument},
                    {.access = AccessMode::Read, .expression = argument},
                },
            .callee_failures = BodyFailures(builder.add_empty_failure_term()),
        }
    );
    auto graph = std::move(caller).finish(
        SemanticRegion {
            .lifetime = lifetime,
            .origin = facts.origin,
            .statements = {{
                .origin = facts.origin,
                .lifetime = lifetime,
                .reachable = true,
                .value = SemExpressionStatement {.expression = std::move(expression)},
            }},
            .result = std::nullopt,
            .result_reachable = false,
            .failures = BodyFailures(builder.add_empty_failure_term()),
            .exits_test = false,
        }
    );
    graph.residual = graph.region;
    publish(std::move(graph), builder);
    ct::expect(expect_termination("semir-residual-call-unresolved", [&] noexcept {
        static_cast<void>(std::move(builder).finish());
    }));
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "SemIR publication: only executable bodies survive the static stage",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            const { let value = 1; assert(value == 1); }
            const test "checked" { let value = 2; check(value == 2); }
            fn select(value: i32, const enabled: bool) -> i32 {
                const offset = 2;
                const if enabled { return value + offset; }
                else { return value; }
            }
            fn use() -> i32 => select(3, true);
            test "runtime" { check(use() == 5); }
        )");
            ct::expect_equal(program.bodies().size(), 3uz);
            ct::expect_equal(program.static_instances().size(), 1uz);
            for (const auto entry : program.declarations().callables()) {
                const auto body = callable_body_id(entry.value);
                if (program.definition_placement(entry.id) == DefinitionPlacement::None) {
                    ct::expect(!body);
                } else {
                    ct::require(body.has_value());
                    ct::expect(program.bodies().body(*body).id() == *body);
                    ct::expect(program.declarations().callable_for_body(*body) == entry.id);
                }
            }
            for (const auto entry : program.tests().entries()) {
                ct::expect(entry.value.body.has_value() != entry.value.is_const);
                if (entry.value.body) {
                    ct::expect(program.bodies().body(*entry.value.body).kind() == BodyKind::Test);
                }
            }
            for (const auto entry : program.bodies().entries()) {
                ct::expect(entry.id == entry.value.id());
                ct::expect(program.executes(entry.id));
                ct::expect(!entry.value.specialized());
                ct::expect(&entry.value.region() == &entry.value.realized_region());
                visit_semantic_nodes(entry.value.region(), [](const auto& node) static noexcept {
                    using Node = std::remove_cvref_t<decltype(node)>;
                    if constexpr (std::same_as<Node, SemanticStatement>) {
                        ct::expect(!std::holds_alternative<SemStaticBinding>(node.value));
                        ct::expect(!std::holds_alternative<SemConstBlock>(node.value));
                        if (const auto* loop = std::get_if<SemRangeLoop>(&node.value)) {
                            ct::expect(!loop->is_static);
                        }
                    } else if constexpr (std::same_as<Node, SemanticExpression>) {
                        if (const auto* branch = std::get_if<SemIf>(&node.value)) {
                            ct::expect(!branch->is_static);
                        }
                    }
                });
            }
        }
    );

    ct::test("SemIR publication: static-only modules publish no bodies", [] static noexcept {
        const auto program = analyze_test_program(R"(
            const { let value = 1; assert(value == 1); }
            const test "checked" { check(true); }
            fn unused(const amount: i32) -> i32 => amount;
        )");
        ct::expect_equal(program.bodies().size(), 0uz);
        ct::expect_equal(program.tests().size(), 1uz);
        for (const auto entry : program.tests().entries()) {
            ct::expect(!entry.value.body);
        }
    });

    ct::test(
        "SemIR publication invariant: residual staged calls require an instance",
        [] static noexcept { check_unresolved_residual_call(); }
    );
    ct::test(
        "SemIR publication invariant: residual regions reject static control",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                begin_compilation(sources, diagnostics, "semir.publication.static_control");
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
                    .items = {test},
                }
            );
            builder.finish_declaration_heads();
            auto reservation = builder.reserve_body(BodyKind::Test);
            builder.define_test(
                test,
                TestDeclaration {
                    .is_const = false,
                    .module_id = module_id,
                    .source =
                        {.label = builder.intern_spelling("static control"),
                         .origin = facts.origin},
                    .body = reservation.id(),
                }
            );
            auto body = BodyBuilder(std::move(reservation), builder);
            const auto lifetime =
                body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, facts.origin);
            auto expression = body.make_expression(
                builder.builtin_type(BuiltinType::Void),
                lifetime,
                facts.origin,
                SemIf {.branches = {}, .otherwise = std::nullopt, .is_static = false}
            );
            auto graph = std::move(body).finish(
                SemanticRegion {
                    .lifetime = lifetime,
                    .origin = facts.origin,
                    .statements =
                        {{.origin = facts.origin,
                          .lifetime = lifetime,
                          .reachable = true,
                          .value = SemExpressionStatement {.expression = std::move(expression)}}},
                    .result = std::nullopt,
                    .result_reachable = false,
                    .failures = BodyFailures(builder.add_empty_failure_term()),
                    .exits_test = false,
                }
            );
            auto residual = graph.region;
            std::get<SemIf>(
                std::get<SemExpressionStatement>(residual.statements.front().value).expression.value
            )
                .is_static = true;
            graph.residual = std::move(residual);
            publish(std::move(graph), builder);
            ct::expect(expect_termination("semir-residual-static-control", [&] noexcept {
                static_cast<void>(std::move(builder).finish());
            }));
        }
    );
    ct::test(
        "SemIR publication: declarations retain body identities when static bodies are removed",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder = begin_compilation(sources, diagnostics, "semir.publication.complete");
            const auto facts = module_facts(builder);
            const auto void_type = builder.builtin_type(BuiltinType::Void);
            const auto integer_type = builder.builtin_type(BuiltinType::I32);
            const auto text_type = builder.builtin_type(BuiltinType::Str);
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

            builder.define_callable_contract(
                function_callable,
                callable_contract(builder, void_type)
            );
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
                    .is_const = false,
                }
            );
            builder.define_declaration(
                structure,
                ConstructionStructDeclaration {
                    .kind = RecordKind::Struct,
                    .module_id = module_id,
                    .name = builder.intern_spelling("Structure"),
                    .origin = facts.origin,
                    .visibility = DeclarationVisibility::Module,
                    .fields = {},
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
                    .supports_equality = true,
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

            auto static_body = builder.reserve_body(BodyKind::ConstBlock);
            const auto removed_body_id = static_body.id();
            auto static_graph = minimal_body(std::move(static_body), facts.origin, builder);

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
                    .source =
                        {.label = builder.intern_spelling("publication topology"),
                         .origin = facts.origin},
                    .body = test_body_id,
                }
            );
            auto test_graph = minimal_body(std::move(test_body), facts.origin, builder);

            auto graphs = std::vector<StructuredBodyDraft>();
            graphs.push_back(std::move(static_graph));
            graphs.push_back(std::move(closure_graph));
            graphs.push_back(std::move(function_graph));
            graphs.push_back(std::move(test_graph));
            publish(std::move(graphs), builder);
            auto finished = std::move(builder).finish();
            if (!ct::expect(finished.has_value())) {
                return;
            }
            const auto program = std::move(*finished);

            ct::expect(!program.bodies().contains(removed_body_id));
            ct::expect_equal(program.bodies().size(), 3uz);
            const auto& published_module = program.declarations().module_decl(module_id);
            ct::expect_equal(published_module.items.size(), 5uz);
            ct::expect(std::ranges::contains(published_module.items, ModuleItem {function}));
            ct::expect(std::ranges::contains(published_module.items, ModuleItem {structure}));
            ct::expect(std::ranges::contains(published_module.items, ModuleItem {enumeration}));
            ct::expect(std::ranges::contains(published_module.items, ModuleItem {module_constant}));
            ct::expect(std::ranges::contains(published_module.items, ModuleItem {test}));
            ct::expect(((program.declarations().enumeration(enumeration).cases)
                        == (std::vector {enum_case})))
                .note(
                    "program.declarations().enumeration(enumeration).cases == std::vector {enum_case}"
                );
            ct::expect(((program.declarations().enum_case(enum_case).owner) == (enumeration)))
                .note("program.declarations().enum_case(enum_case).owner == enumeration");
            ct::expect(((program.declarations().body_for_callable(function_callable))
                        == (function_body_id)))
                .note(
                    "program.declarations().body_for_callable(function_callable) == function_body_id"
                );
            ct::expect(((program.declarations().body_for_callable(closure_callable))
                        == (closure_body_id)))
                .note(
                    "program.declarations().body_for_callable(closure_callable) == closure_body_id"
                );
            ct::expect(((program.declarations().callable_for_body(function_body_id))
                        == (function_callable)))
                .note(
                    "program.declarations().callable_for_body(function_body_id) == function_callable"
                );
            ct::expect(((program.declarations().callable_for_body(closure_body_id))
                        == (closure_callable)))
                .note(
                    "program.declarations().callable_for_body(closure_body_id) == closure_callable"
                );
            ct::expect(!(program.declarations().callable_for_body(test_body_id).has_value()));
            ct::expect(((program.tests().test(test).body) == (test_body_id)))
                .note("program.tests().test(test).body == test_body_id");
            ct::expect(diagnostics.empty());
        }
    );

    ct::test(
        "SemIR publication: semantic module order is independent of provenance order",
        [] static noexcept {
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
            if (!ct::expect(finished.has_value())) {
                return;
            }
            const auto program = std::move(*finished);
            ct::expect(((program.declarations().module_decl(first_module).provenance_module)
                        == (second_facts.provenance_module)))
                .note(
                    "program.declarations().module_decl(first_module).provenance_module == second_facts.provenance_module"
                );
            ct::expect(((program.declarations().module_decl(second_module).provenance_module)
                        == (first_facts.provenance_module)))
                .note(
                    "program.declarations().module_decl(second_module).provenance_module == first_facts.provenance_module"
                );
            ct::expect(diagnostics.empty());
        }
    );

    ct::test(
        "SemIR publication invariant: every failure-set member is nominal",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                begin_compilation(sources, diagnostics, "semir.publication.failure_member");
            const auto facts = module_facts(builder);
            const auto module_id = builder.reserve_module_declaration();
            const auto boolean = builder.builtin_type(BuiltinType::Bool);
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
            ct::expect(expect_termination("semir-publication-nonnominal-failure", [&] noexcept {
                static_cast<void>(std::move(builder).finish());
            }));
        }
    );

    ct::test(
        "SemIR publication invariant: every constant matches its canonical type",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                begin_compilation(sources, diagnostics, "semir.publication.constant_fact");
            const auto facts = module_facts(builder);
            const auto module_id = builder.reserve_module_declaration();
            const auto integer = builder.builtin_type(BuiltinType::I32);
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
            ct::expect(expect_termination("semir-publication-constant-type", [&] noexcept {
                static_cast<void>(std::move(builder).finish());
            }));
        }
    );

    ct::test(
        "SemIR publication: type contents belong to the completed program",
        [] static noexcept {
            const auto program =
                analyze_test_program("struct Record { text: String } fn consume(value: Record) {}");
            ct::expect(!(program.type_contents(program.types().builtin_type(BuiltinType::I32))
                             .read_borrows_storage()));
            ct::expect(program.type_contents(program.types().builtin_type(BuiltinType::String))
                           .read_borrows_storage());
            for (const auto [id, type] : program.types().entries()) {
                if (std::holds_alternative<StructTypeValue>(type.value)) {
                    ct::expect(program.type_contents(id).contains_storage_owner);
                    ct::expect(program.type_contents(id).read_borrows_storage());
                }
            }
            const auto foreign = analyze_test_program("fn unrelated() {}");
            ct::expect(expect_termination("published-type-contents-foreign-identity", [&] noexcept {
                static_cast<void>(
                    program.type_contents(foreign.types().builtin_type(BuiltinType::I32))
                );
            }));
        }
    );
});

} // namespace
