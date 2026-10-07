module carven:test.internal.semantic.analysis.async_catalog;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.semir.async;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Async catalog: compiler declarations belong only to canonical standard module"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto names = std::array {
                std::string_view("crafts.carven.std.async"),
                std::string_view("crafts.demo.async")
            };
            auto inputs = std::vector<SourceModuleInput>();
            for (const auto name : names) {
                const auto source = sources.append_virtual(std::string(name) + ".cv", "");
                const auto path = CanonicalModulePath::from_value(name);
                require(source.has_value());
                require(path.has_value());
                inputs.push_back(SourceModuleInput {.source_id = *source, .module_path = *path});
            }
            auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
            require(syntax.has_value());
            auto diagnostics = DiagnosticSink();
            auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
            auto catalog = build_analysis_catalog(draft);
            require(catalog.has_value());
            const auto view = catalog->view();
            expect(view.symbols().size() == 4);
            expect(view.modules()[0].items.empty());
            expect(view.modules()[1].symbols.empty());
            const auto expected = std::array {
                std::pair {std::string_view("cancel"), AsyncIntrinsic::CancelChild},
                std::pair {
                    std::string_view("cancellation_requested"),
                    AsyncIntrinsic::CancellationRequested
                },
                std::pair {
                    std::string_view("cancellation_point"),
                    AsyncIntrinsic::CancellationPoint
                },
                std::pair {std::string_view("yield_once"), AsyncIntrinsic::YieldOnce},
            };
            for (const auto& symbol : view.symbols()) {
                const auto* intrinsic = std::get_if<CatalogAsyncIntrinsicForm>(&symbol.form);
                require(intrinsic != nullptr);
                expect(!symbol.item_id.has_value());
                const auto declaration =
                    draft.async_intrinsic_declaration_copy(intrinsic->declaration);
                expect(declaration.module_id == view.modules()[0].declaration);
                expect(draft.spelling_copy(declaration.name) == symbol.name);
                const auto match = std::ranges::find(
                    expected,
                    symbol.name,
                    &decltype(expected)::value_type::first
                );
                require(match != expected.end());
                expect(declaration.kind == match->second);
            }
        };
});

} // namespace
