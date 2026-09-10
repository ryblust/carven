module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.interpolation;

import :test.internal.frontend.parse.fixture;
import std;

TEST_CASE("Parser: interpolation preserves expressions and specification spans") {
    const auto source =
        std::string_view(R"(fn value(x: f64, width: i32) { let s = f"value {x:{width}.2f}"; })");
    const auto tree = parse_valid(source);
    const auto ast = tree.view();
    // The body initializer is an ordinary expression whose parts retain their source locations.
    const auto& statement = ast.statement(function_body(tree).statements.front());
    const auto& binding = get<ASTVariableDecl>(statement);
    const auto& interpolation = get<ASTInterpolationExpr>(ast.expression(*binding.initializer));
    REQUIRE(interpolation.parts.size() == 2uz);
    const auto* hole = std::get_if<ASTInterpolationHole>(&interpolation.parts[1].value);
    REQUIRE(hole != nullptr);
    REQUIRE(hole->colon_span.has_value());
    CHECK(slice(source, *hole->colon_span) == ":");
    CHECK(slice(source, ast.expression(hole->expression).span) == "x");
    REQUIRE(hole->specification.size() == 2uz);
    CHECK(slice(source, hole->specification[0].span) == "{width}");
    CHECK(slice(source, hole->specification[1].span) == ".2f");
}

TEST_CASE(
    "Parser: interpolation requires expressions and ordinary literal positions stay literal"
) {
    const auto sources = std::array {
        R"(fn bad() { let s = f"{}"; })",
        R"(fn bad() { let s = f"{:04x}"; })",
        R"(fn bad() { let s = f"{1 +}"; })",
        R"(fn bad() { let s = f"{1:{}}"; })",
        R"(test f"name" {})",
        R"(fn bad(s: str) { match s { f"x" => {}, } })",
    };
    for (const auto* source : sources) {
        check_rejected(source);
    }
}
