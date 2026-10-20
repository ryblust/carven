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
import :test.harness.death;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

namespace {

auto make_body_with_closure(
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
            .reachable = true,
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
            .result_reachable = false,
            .failures = BodyFailures(program.add_empty_failure_term()),
            .exits_call = false,


        }
    );
}

const TestSuite suite([] static noexcept {
    "SemIR publication: only executable bodies survive the static stage"_test = [] static noexcept {
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
        expect_equal(program.bodies().size(), 3uz);
        expect_equal(program.static_instances().size(), 1uz);
        for (const auto entry : program.declarations().callables()) {
            const auto body = callable_body_id(entry.value);
            if (program.is_source_template(entry.id)) {
                expect(!body);
            } else {
                require(body.has_value());
                expect(program.bodies().body(*body).id() == *body);
                expect(program.declarations().callable_for_body(*body) == entry.id);
            }
        }
        for (const auto entry : program.tests().entries()) {
            expect(entry.value.body.has_value() != entry.value.is_const);
            if (entry.value.body) {
                expect(program.bodies().body(*entry.value.body).kind() == BodyKind::Test);
            }
        }
        for (const auto entry : program.bodies().entries()) {
            expect(entry.id == entry.value.id());
            expect(program.executes(entry.id));
            expect(!entry.value.specialized());
            expect(&entry.value.region() == &entry.value.realized_region());
            visit_semantic_nodes(entry.value.region(), [](const auto& node) static noexcept {
                using Node = std::remove_cvref_t<decltype(node)>;
                if constexpr (std::same_as<Node, SemanticStatement>) {
                    expect(!std::holds_alternative<SemStaticBinding>(node.value));
                    expect(!std::holds_alternative<SemConstBlock>(node.value));
                    if (const auto* loop = std::get_if<SemRangeLoop>(&node.value)) {
                        expect(!loop->is_static);
                    }
                } else if constexpr (std::same_as<Node, SemanticExpression>) {
                    if (const auto* branch = std::get_if<SemIf>(&node.value)) {
                        expect(!branch->is_static);
                    }
                }
            });
        }
    };

    "SemIR publication: static-only modules publish no bodies"_test = [] static noexcept {
        const auto program = analyze_test_program(R"(
            const { let value = 1; assert(value == 1); }
            const test "checked" { check(true); }
            fn unused(const amount: i32) -> i32 => amount;
        )");
        expect_equal(program.bodies().size(), 0uz);
        expect_equal(program.tests().size(), 1uz);
        for (const auto entry : program.tests().entries()) {
            expect(!entry.value.body);
        }
    };

    "SemIR publication: closure declarations are shared by zero or multiple occurrences"_test =
        [] static noexcept {
            for (const auto count : {0uz, 2uz}) {
                const auto program = analyze_test_program(
                    std::format("fn make() {{ let values = [[]() -> i32 => 7; {}]; }}", count)
                );
                auto closure_count = 0uz;
                auto closure = std::optional<CallableID>();
                for (const auto [callable, declaration] : program.declarations().callables()) {
                    if (const auto* implementation =
                            std::get_if<ClosureBodyImplementation>(&declaration.implementation)) {
                        ++closure_count;
                        closure = callable;
                        expect(
                            program.declarations().callable_for_body(implementation->body)
                            == callable
                        );
                    }
                }
                if (!expect_equal(closure_count, 1uz) || !expect(closure.has_value())) {
                    return;
                }
                auto occurrences = 0uz;
                for (const auto [body_id, body] : program.bodies().entries()) {
                    static_cast<void>(body_id);
                    visit_semantic_nodes(
                        body.region(),
                        [&](const SemanticExpression& value) noexcept {
                            if (const auto* construction = std::get_if<SemClosure>(&value.value)) {
                                expect(construction->callable == *closure);
                                ++occurrences;
                            }
                        }
                    );
                }
                expect_equal(occurrences, count);
            }
        };

    "SemIR publication: declarations retain body identities when static bodies are removed"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                begin_semir_test_compilation(sources, diagnostics, "semir.publication.complete");
            const auto module_origin = make_semir_test_module_origin(builder);
            const auto void_type = builder.builtin_type(BuiltinType::Void);

            const auto module_id = builder.reserve_module_declaration();
            const auto function = builder.reserve_function_declaration();
            const auto function_callable = builder.reserve_callable_declaration();
            const auto test = builder.reserve_test();

            builder.define_callable_contract(
                function_callable,
                make_semir_test_callable_contract(builder, void_type)
            );
            builder.define_declaration(
                function,
                FunctionDeclaration {
                    .module_id = module_id,
                    .name = builder.intern_spelling("function"),
                    .origin = module_origin.origin,
                    .visibility = DeclarationVisibility::Module,
                    .callable = function_callable,
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
                    .items = {function, test},
                }
            );
            builder.finish_declaration_heads();

            auto static_body = builder.reserve_body(BodyKind::ConstBlock);
            const auto removed_body_id = static_body.id();
            auto static_graph =
                make_semir_test_body(std::move(static_body), module_origin.origin, builder);

            auto function_body = builder.reserve_body(BodyKind::Function);
            const auto function_body_id = function_body.id();
            builder.complete_callable(
                function_callable,
                FunctionBodyImplementation {.body = function_body_id}
            );

            const auto closure_callable =
                builder.append_body_callable(make_semir_test_callable_contract(builder, void_type));
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
            auto function_graph = make_body_with_closure(
                std::move(function_body),
                module_origin.origin,
                closure_type,
                closure_callable,
                builder
            );
            auto closure_graph =
                make_semir_test_body(std::move(closure_body), module_origin.origin, builder);

            auto test_body = builder.reserve_body(BodyKind::Test);
            const auto test_body_id = test_body.id();
            builder.define_test(
                test,
                TestDeclaration {
                    .is_const = false,
                    .module_id = module_id,
                    .source =
                        {.label = builder.intern_spelling("publication topology"),
                         .origin = module_origin.origin},
                    .body = test_body_id,
                }
            );
            auto test_graph =
                make_semir_test_body(std::move(test_body), module_origin.origin, builder);

            builder.add_body_draft(std::move(static_graph));
            builder.add_body_draft(std::move(closure_graph));
            builder.add_body_draft(std::move(function_graph));
            builder.add_body_draft(std::move(test_graph));
            auto finished = std::move(builder).finish();
            if (!expect(finished.has_value())) {
                return;
            }
            const auto program = std::move(*finished);

            expect(!program.bodies().contains(removed_body_id));
            expect_equal(program.bodies().size(), 3uz);
            expect(((program.declarations().body_for_callable(function_callable))
                    == (function_body_id)))
                .note(
                    "program.declarations().body_for_callable(function_callable) == function_body_id"
                );
            expect(((program.declarations().body_for_callable(closure_callable))
                    == (closure_body_id)))
                .note(
                    "program.declarations().body_for_callable(closure_callable) == closure_body_id"
                );
            expect(((program.declarations().callable_for_body(function_body_id))
                    == (function_callable)))
                .note(
                    "program.declarations().callable_for_body(function_body_id) == function_callable"
                );
            expect(((program.declarations().callable_for_body(closure_body_id))
                    == (closure_callable)))
                .note(
                    "program.declarations().callable_for_body(closure_body_id) == closure_callable"
                );
            expect(!(program.declarations().callable_for_body(test_body_id).has_value()));
            expect(((program.tests().test(test).body) == (test_body_id)))
                .note("program.tests().test(test).body == test_body_id");
            expect(diagnostics.empty());
        };

    "SemIR publication: semantic module order is independent of provenance order"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            constexpr auto module_names = std::array<std::string_view, 2uz> {
                "semir.publication.order_first",
                "semir.publication.order_second",
            };
            auto builder = begin_semir_test_compilation_batch(sources, diagnostics, module_names);
            const auto first_module_origin = make_semir_test_module_origin(builder, 0uz);
            const auto second_module_origin = make_semir_test_module_origin(builder, 1uz);
            const auto first_module = builder.reserve_module_declaration();
            const auto second_module = builder.reserve_module_declaration();
            builder.define_declaration(
                first_module,
                ModuleDeclaration {
                    .provenance_module = second_module_origin.provenance_module,
                    .origin = second_module_origin.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {},
                }
            );
            builder.define_declaration(
                second_module,
                ModuleDeclaration {
                    .provenance_module = first_module_origin.provenance_module,
                    .origin = first_module_origin.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {},
                }
            );
            builder.finish_declaration_heads();
            auto finished = std::move(builder).finish();
            if (!expect(finished.has_value())) {
                return;
            }
            const auto program = std::move(*finished);
            expect(((program.declarations().module_decl(first_module).provenance_module)
                    == (second_module_origin.provenance_module)))
                .note(
                    "program.declarations().module_decl(first_module).provenance_module == second_module_origin.provenance_module"
                );
            expect(((program.declarations().module_decl(second_module).provenance_module)
                    == (first_module_origin.provenance_module)))
                .note(
                    "program.declarations().module_decl(second_module).provenance_module == first_module_origin.provenance_module"
                );
            expect(diagnostics.empty());
        };

    "SemIR publication invariant: every constant matches its canonical type"_test =
        termination_test([] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder = begin_semir_test_compilation(
                sources,
                diagnostics,
                "semir.publication.constant_fact"
            );
            const auto module_origin = make_semir_test_module_origin(builder);
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
                    .provenance_module = module_origin.provenance_module,
                    .origin = module_origin.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {},
                }
            );
            builder.finish_declaration_heads();
            expect_termination([&] noexcept { static_cast<void>(std::move(builder).finish()); });
        });

    "SemIR publication: type contents describe owned and borrowed storage"_test =
        [] static noexcept {
            const auto program =
                analyze_test_program("struct Record { text: String } fn consume(value: Record) {}");
            expect(!(program.type_contents(program.types().builtin_type(BuiltinType::I32))
                         .read_borrows_storage()));
            expect(program.type_contents(program.types().builtin_type(BuiltinType::String))
                       .read_borrows_storage());
            for (const auto [id, type] : program.types().entries()) {
                if (std::holds_alternative<StructTypeValue>(type.value)) {
                    expect(program.type_contents(id).contains_storage_owner);
                    expect(program.type_contents(id).read_borrows_storage());
                }
            }
        };
});

} // namespace
