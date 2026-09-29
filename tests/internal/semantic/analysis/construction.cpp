module carven:test.internal.semantic.analysis.construction;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

template<typename Action>
auto with_catalog(std::string source_text, Action action) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("construction.cv", std::move(source_text));
    if (!ct::expect(source.has_value())) {
        return;
    }
    auto path = CanonicalModulePath::from_value("construction");
    if (!ct::expect(path.has_value())) {
        return;
    }
    const auto inputs = std::array {
        SourceModuleInput {.source_id = *source, .module_path = std::move(*path)},
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    if (!ct::expect(syntax.has_value())) {
        return;
    }
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    if (!ct::expect(catalog.has_value())) {
        return;
    }
    auto usage = ImportUsage(catalog->view().imports().size());
    action(draft, catalog->view(), usage, diagnostics);
}

auto function_named(AnalysisCatalogView catalog, std::string_view name) noexcept
    -> CatalogFunctionForm {
    const auto found = std::ranges::find(catalog.symbols(), name, &CatalogSymbol::name);
    ct::require(found != catalog.symbols().end());
    const auto* function = std::get_if<CatalogFunctionForm>(&found->form);
    ct::require(function != nullptr);
    return *function;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Program construction: demand bodies reuse completion and keep stable references",
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
                    ct::expect(
                        draft.module_declaration_copy(catalog.modules().front().declaration)
                            .provenance_module
                        == module_id
                    );
                    ct::expect(!(draft.function_for_callable(seed.callable).has_value()));
                    ct::expect(expect_termination("unprepared-function-head-read", [&] noexcept {
                        static_cast<void>(draft.function_declaration_copy(last.function));
                    }));
                    ct::expect(expect_termination("uncompleted-body-read", [&] noexcept {
                        const auto reserved = draft.reserve_body(BodyKind::Function);
                        static_cast<void>(draft.body_draft(reserved.id()));
                    }));
                    auto first_body =
                        construction.construction_requests()
                            .ensure_function_body(seed.function, module_id, Span::at(0u))
                            .run();
                    if (!ct::expect(first_body.has_value())) {
                        return;
                    }
                    const auto* saved = std::addressof(draft.body_draft(*first_body));
                    ct::expect(draft.function_for_callable(seed.callable) == seed.function);
                    ct::expect(!(draft.function_for_callable(last.callable).has_value()));

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
                        if (!ct::expect(body.has_value())) {
                            return;
                        }
                        completed_bodies.insert(*body);
                        ct::expect(std::addressof(draft.body_draft(*first_body)) == saved);
                    }
                    ct::expect_equal(completed_bodies.size(), 35uz);
                    const auto repeated =
                        construction.construction_requests()
                            .ensure_function_body(seed.function, module_id, Span::at(0u))
                            .run();
                    if (!ct::expect(repeated.has_value())) {
                        return;
                    }
                    ct::expect(*repeated == *first_body);
                    if (!ct::expect(construction.run().has_value())) {
                        return;
                    }
                    ct::expect(std::addressof(draft.body_draft(*first_body)) == saved);
                    auto program = std::move(draft).finish();
                    if (!ct::expect(program.has_value())) {
                        return;
                    }
                    ct::expect_equal(program->bodies().size(), 36uz);
                }
            );
        }
    );

    ct::test(
        "Construction: pending results require completion before contract access",
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
                    if (!ct::expect(construction.construction_requests()
                                        .ensure_declaration(
                                            catalog.function_symbol(function.function),
                                            module_id,
                                            Span::at(0u)
                                        )
                                        .run()
                                        .has_value())) {
                        return;
                    }
                    if (!ct::expect(
                            draft.pending_function_contract_copy(function.callable).has_value()
                        )) {
                        return;
                    }
                    ct::expect(expect_termination("pending-function-contract-read", [&] noexcept {
                        static_cast<void>(
                            draft.construction_callable_contract_copy(function.callable)
                        );
                    }));
                    if (!ct::expect(construction.construction_requests()
                                        .ensure_function_signature(
                                            function.function,
                                            module_id,
                                            Span::at(0u)
                                        )
                                        .run()
                                        .has_value())) {
                        return;
                    }
                    ct::expect(
                        !(draft.pending_function_contract_copy(function.callable).has_value())
                    );
                    ct::expect(
                        expect_termination("duplicate-function-result-completion", [&] noexcept {
                            draft.complete_function_result(
                                function.callable,
                                draft.builtin_type(BuiltinType::I32)
                            );
                        })
                    );
                    if (!ct::expect(construction.run().has_value())) {
                        return;
                    }
                    ct::expect(std::move(draft).finish().has_value());
                }
            );
        }
    );

    ct::test(
        "Program construction: long inferred dependencies complete or diagnose the leaf",
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
                            ct::expect(construction.run().has_value() == !failed);
                        }
                    );
                }
            }
        }
    );

    ct::test("Analysis catalog: enum name lookup respects program identity", [] static noexcept {
        with_catalog(
            "enum Choice { First, Second }",
            [](ProgramDraft&,
               AnalysisCatalogView catalog,
               ImportUsage&,
               DiagnosticSink&) static noexcept {
                const auto found =
                    std::ranges::find(catalog.symbols(), "Choice", &CatalogSymbol::name);
                if (!ct::expect(found != catalog.symbols().end())) {
                    return;
                }
                const auto* form = std::get_if<CatalogEnumForm>(&found->form);
                if (!ct::expect(form != nullptr)) {
                    return;
                }
                const auto enumeration = form->enumeration;
                if (!ct::expect(catalog.enum_case_named(enumeration, "First").has_value())) {
                    return;
                }
                ct::expect(!(catalog.enum_case_named(enumeration, "Missing").has_value()));
                with_catalog(
                    "enum Choice { First, Second }",
                    [&](ProgramDraft&,
                        AnalysisCatalogView other,
                        ImportUsage&,
                        DiagnosticSink&) noexcept {
                        ct::expect(expect_termination("enum-name-foreign-owner", [&] noexcept {
                            static_cast<void>(other.enum_case_named(enumeration, "First"));
                        }));
                    }
                );
            }
        );
    });
});

} // namespace
