module carven:test.internal.frontend.program;

import :frontend.program;
import :frontend.program.parse;
import :frontend.program.verify;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    ct::require(result.has_value());
    return std::move(*result);
}

} // namespace

static_assert(!std::copy_constructible<SyntaxProgram>);
static_assert(std::move_constructible<SyntaxProgram>);
static_assert(!std::constructible_from<SyntaxProgram, SyntaxProgramParts>);

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Syntax program: publication gate correlates roots, provenance, and imports",
        [] static noexcept {
            auto sources = SourceManager();
            const auto main_source = sources.append_virtual(
                "main.cv",
                "import .model using *;\n"
                "fn inspect(value) {\n"
                " let made = Model {};\n"
                " match value { is [i32; 4] => made }\n"
                "}\n"
            );
            const auto model_source =
                sources.append_virtual("model.cv", "struct Model { value: i32, }\n");
            if (!ct::expect(main_source.has_value())) {
                return;
            }
            if (!ct::expect(model_source.has_value())) {
                return;
            }

            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = *main_source,
                    .module_path = path("app.main"),
                },
                SourceModuleInput {
                    .source_id = *model_source,
                    .module_path = path("app.model"),
                },
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
            if (!ct::expect(parsed.has_value())) {
                return;
            }
            const auto& program = *parsed;
            if (!ct::expect(verify_syntax_program(program).has_value())) {
                return;
            }
            if (!ct::expect_equal(program.provenance().module_records().size(), 2uz)) {
                return;
            }
            if (!ct::expect_equal(program.syntax_trees().size(), 2uz)) {
                return;
            }
            const auto main_module = program.provenance().module_id_at(0);
            const auto model_module = program.provenance().module_id_at(1);
            ct::expect_equal(
                program.provenance().module_record(main_module).path.value(),
                std::string_view("app.main")
            );
            ct::expect(((program.syntax_tree(main_module).view().source_id()) == (*main_source)))
                .note("program.syntax_tree(main_module).view().source_id() == *main_source");
            ct::expect(((program.syntax_tree(model_module).view().source_id()) == (*model_source)))
                .note("program.syntax_tree(model_module).view().source_id() == *model_source");
            if (!ct::expect_equal(program.resolved_import_graph().size(), 2uz)) {
                return;
            }
            const auto main_imports = program.resolved_imports(main_module);
            if (!ct::expect_equal(main_imports.size(), 1uz)) {
                return;
            }
            if (!ct::expect_equal(
                    program.syntax_tree(main_module).view().ast_module().module_imports.size(),
                    1uz
                )) {
                return;
            }
            ct::expect(
                ((main_imports.front().declaration)
                 == (program.syntax_tree(main_module).view().ast_module().module_imports.front()))
            )
                .note(
                    "main_imports.front().declaration == program.syntax_tree(main_module).view().ast_module().module_imports.front()"
                );
            ct::expect(((main_imports.front().target) == (model_module)))
                .note("main_imports.front().target == model_module");
            ct::expect(program.resolved_imports(model_module).empty());
            ct::expect(
                (program.provenance()
                     .source_snapshot(program.provenance().module_record(main_module).source_id)
                     .manager_source_id()
                 == *main_source)
            )
                .note(
                    "program.provenance()\n                    .source_snapshot(program.provenance().module_record(main_module).source_id)\n..."
                );
            ct::expect(
                (program.provenance()
                     .source_snapshot(program.provenance().module_record(model_module).source_id)
                     .manager_source_id()
                 == *model_source)
            )
                .note(
                    "program.provenance()\n                    .source_snapshot(program.provenance().module_record(model_module).source_id)..."
                );
        }
    );
});

} // namespace
