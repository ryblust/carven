module carven:test.internal.semantic.analysis.construction;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

template<typename Action>
auto with_catalog(std::string source_text, Action action) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("construction.cv", std::move(source_text));
    if (!expect(source.has_value())) {
        return;
    }
    auto path = CanonicalModulePath::from_value("construction");
    if (!expect(path.has_value())) {
        return;
    }
    const auto inputs = std::array {
        SourceModuleInput {.source_id = *source, .module_path = std::move(*path)},
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    if (!expect(syntax.has_value())) {
        return;
    }
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    if (!expect(catalog.has_value())) {
        return;
    }
    auto usage = ImportUsage(catalog->view().imports().size());
    action(draft, catalog->view(), usage, diagnostics);
}

auto function_named(AnalysisCatalogView catalog, std::string_view name) noexcept
    -> CatalogFunctionForm {
    const auto found = std::ranges::find(catalog.symbols(), name, &CatalogSymbol::name);
    require(found != catalog.symbols().end());
    const auto* function = std::get_if<CatalogFunctionForm>(&found->form);
    require(function != nullptr);
    return *function;
}

const TestSuite suite([] static noexcept {
    "Program construction: grouped contextual enum names retain Body and Static case identity"_test =
        [] static noexcept {
            constexpr auto case_name =
                std::string_view("ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage");
            expect_equal(case_name.size(), 51uz);
            const auto program = analyze_test_program(R"(
        enum Choice { ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage, Other }
        const selected: Choice = (((.ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage)));
        const matches = selected == Choice::ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage;
        fn body_value() -> Choice => (((.ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage)));
    )");
            auto expected = std::optional<ConstantID>();
            for (const auto [id, declaration] : program.declarations().enum_cases()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) == case_name) {
                    expected = declaration.constant;
                }
            }
            if (!expect(expected.has_value())) {
                return;
            }
            auto saw_selected = false;
            auto saw_matches = false;
            for (const auto [id, declaration] : program.declarations().module_constants()) {
                static_cast<void>(id);
                const auto name = program.provenance().spelling(declaration.name);
                if (name == "selected") {
                    expect(declaration.value == *expected).note("Static selected case identity");
                    saw_selected = true;
                } else if (name == "matches") {
                    const auto* value = std::get_if<BooleanConstant>(
                        &program.constants().constant(declaration.value).value
                    );
                    if (!expect(value != nullptr)) {
                        return;
                    }
                    expect_equal(value->value, true);
                    saw_matches = true;
                }
            }
            expect_equal(saw_selected, true);
            expect_equal(saw_matches, true);
            auto body_constants = 0uz;
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) != "body_value") {
                    continue;
                }
                const auto body = program.declarations().body_for_callable(declaration.callable);
                if (!expect(body.has_value())) {
                    return;
                }
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* constant = std::get_if<SemConstant>(&expression.value);
                        if (constant == nullptr) {
                            return;
                        }
                        expect(constant->constant == *expected).note("Body selected case identity");
                        ++body_constants;
                    }
                );
            }
            expect_equal(body_constants, 1uz);
        };

    "Program construction: contextual enum diagnostics retain prerequisite order and name spans"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
                DiagnosticCode expected;
                DiagnosticCode excluded;
                std::string_view primary;
            };
            const auto cases = std::to_array<Scenario>({
                {
                    .name = "Body nonenum context precedes missing case lookup",
                    .source =
                        "fn invalid() { let value: i32 = .ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage; }",
                    .expected = DiagnosticCode::TypeEnumContext,
                    .excluded = DiagnosticCode::TypeMemberUnresolved,
                    .primary = "ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage",
                },
                {
                    .name = "Static nonenum context precedes missing case lookup",
                    .source =
                        "const invalid: i32 = .ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage;",
                    .expected = DiagnosticCode::TypeEnumContext,
                    .excluded = DiagnosticCode::TypeMemberUnresolved,
                    .primary = "ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage",
                },
                {
                    .name = "Body enum context diagnoses its missing case",
                    .source =
                        "enum Choice { Existing }\nfn invalid() { let value: Choice = .ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage; }",
                    .expected = DiagnosticCode::TypeMemberUnresolved,
                    .excluded = DiagnosticCode::TypeEnumContext,
                    .primary = "ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage",
                },
                {
                    .name = "Static enum context diagnoses its missing case",
                    .source =
                        "enum Choice { Existing }\nconst invalid: Choice = .ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage;",
                    .expected = DiagnosticCode::TypeMemberUnresolved,
                    .excluded = DiagnosticCode::TypeEnumContext,
                    .primary = "ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage",
                },
                {
                    .name = "Static owner validity precedes missing case lookup",
                    .source =
                        "const invalid = Empty::ThisIsAnEnumCaseNameLongEnoughToUseOwnedHeapStorage;\nenum Empty {}",
                    .expected = DiagnosticCode::TypeEnumEmpty,
                    .excluded = DiagnosticCode::TypeMemberUnresolved,
                    .primary = "Empty",
                },
            });
            each(cases, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto sources = SourceManager();
                const auto source =
                    sources.append_virtual("analysis.cv", std::string(scenario.source));
                require(source.has_value());
                const auto input = SourceModuleInput {
                    .source_id = *source,
                    .module_path = semantic_test_module_path(),
                };
                auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
                require(parsed.has_value());
                const auto analyzed = analyze(std::move(*parsed));
                if (!expect(!analyzed.has_value())) {
                    return;
                }
                expect_diagnostic(analyzed.error(), scenario.expected);
                expect_no_diagnostic(analyzed.error(), scenario.excluded);
                const auto* diagnostic = find_diagnostic(analyzed.error(), scenario.expected);
                if (!expect(diagnostic != nullptr)) {
                    return;
                }
                expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
                if (!expect(diagnostic->attachment.primary.has_value())) {
                    return;
                }
                expect_equal(sources.slice(diagnostic->attachment.primary->span), scenario.primary);
                const auto start = scenario.source.rfind(scenario.primary);
                require(start != std::string_view::npos);
                expect_equal(
                    diagnostic->attachment.primary->span.span.start(),
                    static_cast<std::uint32_t>(start)
                );
            });
        };

    "Program construction: demand bodies reuse completion and keep stable references"_test =
        [] static noexcept {
            auto source = std::string(R"(
        const marker = 41;
        struct Holder { value: i32 }
        enum Choice { Named(Holder), Empty }
        fn seed() -> i32 { let wrapped = Holder { value: marker }; return wrapped.value; }
        fn typed(value: Choice) -> Choice { return value; }
        fn closure() -> i32 { let call = []() => seed(); return call(); }
    )");
            for (auto index = 0uz; index < 32uz; ++index) {
                source += std::format("fn value_{}() => {};\n", index, index);
            }
            with_catalog(
                std::move(source),
                [](ProgramDraft& draft,
                   AnalysisCatalogView catalog,
                   ImportUsage& usage,
                   DiagnosticSink&) static noexcept {
                    auto construction = ProgramConstruction(draft, catalog, usage);
                    const auto module_id = catalog.modules().front().module_id;
                    const auto seed = function_named(catalog, "seed");
                    const auto last = function_named(catalog, "value_31");
                    expect(
                        draft.module_declaration_copy(catalog.modules().front().declaration)
                            .provenance_module
                        == module_id
                    );
                    expect(!(draft.function_for_callable(seed.callable).has_value()));
                    expect(expect_termination("unprepared-function-head-read", [&] noexcept {
                        static_cast<void>(draft.function_declaration_copy(last.function));
                    }));
                    expect(expect_termination("uncompleted-body-read", [&] noexcept {
                        const auto reserved = draft.reserve_body(BodyKind::Function);
                        static_cast<void>(draft.body_draft(reserved.id()));
                    }));
                    auto first_body =
                        construction.construction_requests()
                            .ensure_function_body(seed.function, module_id, Span::at(0u))
                            .run();
                    if (!expect(first_body.has_value())) {
                        return;
                    }
                    const auto* saved = std::addressof(draft.body_draft(*first_body));
                    expect(draft.function_for_callable(seed.callable) == seed.function);
                    expect(!(draft.function_for_callable(last.callable).has_value()));

                    auto completed_bodies = std::flat_set<BodyID> {*first_body};
                    for (const auto& symbol : catalog.symbols()) {
                        const auto* function = std::get_if<CatalogFunctionForm>(&symbol.form);
                        if (function == nullptr) {
                            continue;
                        }
                        const auto body = construction.construction_requests()
                                              .ensure_function_body(
                                                  function->function,
                                                  symbol.module_id,
                                                  symbol.declaration_span
                                              )
                                              .run();
                        if (!expect(body.has_value())) {
                            return;
                        }
                        completed_bodies.insert(*body);
                        expect(std::addressof(draft.body_draft(*first_body)) == saved);
                    }
                    expect_equal(completed_bodies.size(), 35uz);
                    const auto repeated =
                        construction.construction_requests()
                            .ensure_function_body(seed.function, module_id, Span::at(0u))
                            .run();
                    if (!expect(repeated.has_value())) {
                        return;
                    }
                    expect(*repeated == *first_body);
                    if (!expect(construction.run().has_value())) {
                        return;
                    }
                    expect(std::addressof(draft.body_draft(*first_body)) == saved);
                    auto program = std::move(draft).finish();
                    if (!expect(program.has_value())) {
                        return;
                    }
                    expect_equal(program->bodies().size(), 36uz);
                }
            );
        };

    "Construction: pending results require completion before contract access"_test =
        [] static noexcept {
            with_catalog(
                "fn inferred() => 1;",
                [](ProgramDraft& draft,
                   AnalysisCatalogView catalog,
                   ImportUsage& usage,
                   DiagnosticSink&) static noexcept {
                    auto construction = ProgramConstruction(draft, catalog, usage);
                    const auto function = function_named(catalog, "inferred");
                    const auto module_id = catalog.modules().front().module_id;
                    if (!expect(construction.construction_requests()
                                    .ensure_declaration(
                                        catalog.function_symbol(function.function),
                                        module_id,
                                        Span::at(0u)
                                    )
                                    .run()
                                    .has_value())) {
                        return;
                    }
                    if (!expect(
                            draft.pending_function_contract_copy(function.callable).has_value()
                        )) {
                        return;
                    }
                    expect(expect_termination("pending-function-contract-read", [&] noexcept {
                        static_cast<void>(
                            draft.construction_callable_contract_copy(function.callable)
                        );
                    }));
                    if (!expect(construction.construction_requests()
                                    .ensure_function_signature(
                                        function.function,
                                        module_id,
                                        Span::at(0u)
                                    )
                                    .run()
                                    .has_value())) {
                        return;
                    }
                    expect(!(draft.pending_function_contract_copy(function.callable).has_value()));
                    expect(expect_termination("duplicate-function-result-completion", [&] noexcept {
                        draft.complete_function_result(
                            function.callable,
                            draft.builtin_type(BuiltinType::I32)
                        );
                    }));
                    if (!expect(construction.run().has_value())) {
                        return;
                    }
                    expect(std::move(draft).finish().has_value());
                }
            );
        };

    "Program construction: long inferred dependencies complete or diagnose the leaf"_test =
        [] static noexcept {
            for (const auto failed : {false, true}) {
                for (const auto reverse : {false, true}) {
                    auto declarations = std::vector<std::string>();
                    for (auto index = 0uz; index < 256uz; ++index) {
                        declarations.push_back(
                            std::format("fn step_{}() => step_{}();\n", index, index + 1uz)
                        );
                    }
                    declarations.push_back(
                        failed ? "fn step_256() => unknown_leaf;\n" : "fn step_256() => 7;\n"
                    );
                    if (reverse) {
                        std::ranges::reverse(declarations);
                    }
                    auto source = std::string();
                    for (const auto& declaration : declarations) {
                        source += declaration;
                    }
                    with_catalog(
                        std::move(source),
                        [&](ProgramDraft& draft,
                            AnalysisCatalogView catalog,
                            ImportUsage& usage,
                            DiagnosticSink&) noexcept {
                            auto construction = ProgramConstruction(draft, catalog, usage);
                            expect(construction.run().has_value() == !failed);
                        }
                    );
                }
            }
        };
});

} // namespace
