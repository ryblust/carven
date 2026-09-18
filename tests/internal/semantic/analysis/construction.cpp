module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

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
import :test.internal.harness.death;
import std;

namespace {

template<typename Action>
auto with_catalog(std::string source_text, Action action) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("construction.cv", std::move(source_text));
    REQUIRE(source.has_value());
    auto path = CanonicalModulePath::from_value("construction");
    REQUIRE(path.has_value());
    const auto inputs = std::array {
        SourceModuleInput {.source_id = *source, .module_path = std::move(*path)},
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    REQUIRE(catalog.has_value());
    auto usage = ImportUsage(catalog->view().imports().size());
    action(draft, catalog->view(), usage, diagnostics);
}

auto function_named(AnalysisCatalogView catalog, std::string_view name) noexcept
    -> CatalogFunctionForm {
    const auto found = std::ranges::find(catalog.symbols(), name, &CatalogSymbol::name);
    REQUIRE(found != catalog.symbols().end());
    const auto* function = std::get_if<CatalogFunctionForm>(&found->form);
    REQUIRE(function != nullptr);
    return *function;
}

} // namespace

TEST_CASE("Program construction: demand bodies reuse completion and keep stable references") {
    auto source = std::string(R"(
        const marker = 41;
        struct Holder { value: i32 }
        enum Choice { Named(Holder), Empty }
        fn seed() -> i32 { let wrapped = Holder { value: marker }; return wrapped.value; }
        fn typed(value: Choice) -> bool { return value == value; }
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
            auto requests = ProgramConstruction(draft, catalog, usage);
            const auto module_id = catalog.modules().front().module_id;
            const auto seed = function_named(catalog, "seed");
            const auto last = function_named(catalog, "value_31");
            CHECK(
                draft.module_declaration_copy(catalog.modules().front().declaration)
                    .provenance_module
                == module_id
            );
            CHECK_FALSE(draft.function_for_callable(seed.callable).has_value());
            CHECK(expect_termination("unprepared-function-head-read", [&] {
                static_cast<void>(draft.function_declaration_copy(last.function));
            }));
            CHECK(expect_termination("uncompleted-body-read", [&] {
                const auto reserved = draft.reserve_body(BodyKind::Function);
                static_cast<void>(draft.body_draft(reserved.id()));
            }));
            auto first_body =
                requests.ensure_function_body(seed.function, module_id, Span::at(0u)).run();
            REQUIRE(first_body.has_value());
            const auto* saved = std::addressof(draft.body_draft(*first_body));
            CHECK(draft.function_for_callable(seed.callable) == seed.function);
            CHECK_FALSE(draft.function_for_callable(last.callable).has_value());

            auto completed_bodies = std::flat_set<BodyID> {*first_body};
            for (const auto& symbol : catalog.symbols()) {
                const auto* function = std::get_if<CatalogFunctionForm>(&symbol.form);
                if (function == nullptr) {
                    continue;
                }
                const auto body = requests
                                      .ensure_function_body(
                                          function->function,
                                          symbol.module_id,
                                          symbol.declaration_span
                                      )
                                      .run();
                REQUIRE(body.has_value());
                completed_bodies.insert(*body);
                CHECK(std::addressof(draft.body_draft(*first_body)) == saved);
            }
            CHECK_EQ(completed_bodies.size(), 35uz);
            const auto repeated =
                requests.ensure_function_body(seed.function, module_id, Span::at(0u)).run();
            REQUIRE(repeated.has_value());
            CHECK(*repeated == *first_body);
            REQUIRE(requests.run().has_value());
            CHECK(std::addressof(draft.body_draft(*first_body)) == saved);
            auto program = std::move(draft).finish();
            REQUIRE(program.has_value());
            CHECK_EQ(program->bodies().size(), 36uz);
        }
    );
}

TEST_CASE("Construction: pending results require completion before contract access") {
    with_catalog(
        "fn inferred() => 1;",
        [](ProgramDraft& draft,
           AnalysisCatalogView catalog,
           ImportUsage& usage,
           DiagnosticSink&) static noexcept {
            auto construction = ProgramConstruction(draft, catalog, usage);
            const auto function = function_named(catalog, "inferred");
            const auto module_id = catalog.modules().front().module_id;
            REQUIRE(construction
                        .ensure_declaration(
                            catalog.function_symbol(function.function),
                            module_id,
                            Span::at(0u)
                        )
                        .run()
                        .has_value());
            REQUIRE(draft.pending_function_contract_copy(function.callable).has_value());
            CHECK(expect_termination("pending-function-contract-read", [&] {
                static_cast<void>(draft.construction_callable_contract_copy(function.callable));
            }));
            REQUIRE(construction
                        .ensure_function_signature(function.function, module_id, Span::at(0u))
                        .run()
                        .has_value());
            CHECK_FALSE(draft.pending_function_contract_copy(function.callable).has_value());
            CHECK(expect_termination("duplicate-function-result-completion", [&] {
                draft.complete_function_result(
                    function.callable,
                    draft.builtin_type(BuiltinType::I32)
                );
            }));
            REQUIRE(construction.run().has_value());
            CHECK(std::move(draft).finish().has_value());
        }
    );
}

TEST_CASE("Program construction: long inferred dependencies complete or diagnose the leaf") {
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
                    auto requests = ProgramConstruction(draft, catalog, usage);
                    CHECK(requests.run().has_value() == !failed);
                }
            );
        }
    }
}
