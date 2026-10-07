module carven:test.internal.semantic.analysis.async_catalog;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Async catalog: module paths do not create implicit declarations"_test = [] static noexcept {
        struct Case final {
            std::string_view source;
            std::size_t declarations;
        };
        constexpr auto cases = std::array {
            Case {.source = "", .declarations = 0uz},
            Case {.source = "fn yield_once() {}", .declarations = 1uz},
        };
        constexpr auto names = std::array {
            std::string_view("crafts.carven.std.time"),
            std::string_view("crafts.demo.async")
        };
        for (const auto& input : cases) {
            auto sources = SourceManager();
            auto inputs = std::vector<SourceModuleInput>();
            for (const auto name : names) {
                const auto source =
                    sources.append_virtual(std::string(name) + ".cv", std::string(input.source));
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
            expect_equal(view.symbols().size(), input.declarations * names.size())
                .note("source = ", input.source);
            for (const auto& module : view.modules()) {
                expect_equal(module.items.size(), input.declarations)
                    .note("source = ", input.source);
                expect_equal(module.symbols.size(), input.declarations)
                    .note("source = ", input.source);
            }
            for (const auto& symbol : view.symbols()) {
                expect_equal(symbol.name, std::string_view("yield_once"));
                expect(symbol.item_id.has_value());
                expect(std::holds_alternative<CatalogFunctionForm>(symbol.form));
            }
        }
    };
});

} // namespace
