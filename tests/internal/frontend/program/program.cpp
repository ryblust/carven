module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.program;

import :compilation.request;
import :frontend.program;
import :frontend.program.parse;
import :frontend.program.verify;
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

static_assert(!std::copy_constructible<ParsedBatch>);
static_assert(std::movable<ParsedBatch>);

TEST_CASE("Syntax program: a published multi-module owner preserves source correlation") {
    auto sources = SourceManager();
    const auto main_source = sources.append_virtual("main.cv", "const answer: i32 = 42;\n");
    const auto model_source = sources.append_virtual("model.cv", "struct Model { value: i32, }\n");
    REQUIRE(main_source.has_value());
    REQUIRE(model_source.has_value());

    const auto inputs = std::array {
        CompilationInput {
            .source_id = *main_source,
            .module_path = path("app.main"),
        },
        CompilationInput {
            .source_id = *model_source,
            .module_path = path("app.model"),
        },
    };
    auto parsed = parse(sources, inputs);
    REQUIRE(parsed.has_value());
    const auto& program = *parsed;
    REQUIRE(verify_syntax_program(program).has_value());
    REQUIRE_EQ(program.provenance().module_records().size(), 2u);
    REQUIRE_EQ(program.syntax_trees().size(), 2u);
    const auto main_module = ProgramModuleID::from_index(0);
    const auto model_module = ProgramModuleID::from_index(1);
    CHECK_EQ(program.provenance().module_record(main_module).path.value(), "app.main");
    CHECK_EQ(program.syntax_tree(main_module).view().source_id(), *main_source);
    CHECK_EQ(program.syntax_tree(model_module).view().source_id(), *model_source);
    CHECK(program.provenance().find_source_snapshot(*main_source).has_value());
    CHECK(program.provenance().find_source_snapshot(*model_source).has_value());
}
