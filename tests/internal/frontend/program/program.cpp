module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.program;

import :compiler.request;
import :frontend.program.parse;
import :frontend.program.verify;
import :frontend.program;
import :source.manager;
import :source.module_path;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace

static_assert(!std::copy_constructible<SyntaxProgram>);
static_assert(std::movable<SyntaxProgram>);
static_assert(!std::constructible_from<SyntaxProgram, SyntaxProgramParts>);

TEST_CASE("Syntax program: publication gate correlates roots, provenance, and imports") {
    auto sources = SourceManager();
    const auto main_source = sources.append_virtual(
        "main.cv",
        "import .model using *;\n"
        "fn inspect(value) {\n"
        " let made = Model {};\n"
        " match value { is [i32; 4] => made }\n"
        "}\n"
    );
    const auto model_source = sources.append_virtual("model.cv", "struct Model { value: i32, }\n");
    REQUIRE(main_source.has_value());
    REQUIRE(model_source.has_value());

    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *main_source,
            .module_path = path("app.main"),
        },
        CompilationModuleInput {
            .source_id = *model_source,
            .module_path = path("app.model"),
        },
    };
    auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(parsed.has_value());
    const auto& program = *parsed;
    REQUIRE(verify_syntax_program(program).has_value());
    REQUIRE_EQ(program.provenance().module_records().size(), 2u);
    REQUIRE_EQ(program.syntax_trees().size(), 2u);
    const auto main_module = program.provenance().module_id_at(0);
    const auto model_module = program.provenance().module_id_at(1);
    CHECK_EQ(program.provenance().module_record(main_module).path.value(), "app.main");
    CHECK_EQ(program.syntax_tree(main_module).view().source_id(), *main_source);
    CHECK_EQ(program.syntax_tree(model_module).view().source_id(), *model_source);
    REQUIRE_EQ(program.resolved_import_graph().size(), 2u);
    const auto main_imports = program.resolved_imports(main_module);
    REQUIRE_EQ(main_imports.size(), 1u);
    REQUIRE_EQ(program.syntax_tree(main_module).view().ast_module().module_imports.size(), 1u);
    CHECK_EQ(
        main_imports.front().declaration,
        program.syntax_tree(main_module).view().ast_module().module_imports.front()
    );
    CHECK_EQ(main_imports.front().target, model_module);
    CHECK(program.resolved_imports(model_module).empty());
    CHECK(program.provenance().find_source_snapshot(*main_source).has_value());
    CHECK(program.provenance().find_source_snapshot(*model_source).has_value());
}
