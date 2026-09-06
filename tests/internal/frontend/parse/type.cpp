module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.type;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :source.text;
import :test.internal.frontend.parse.fixture;
import std;

TEST_CASE("Parser: recursive types retain grammar structure") {
    static constexpr auto text = std::string_view(
        "fn types(\n"
        " nested: [[u8; width]; height], callback: fn(i32, &State,) -> bool,\n"
        ") {}\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& fn = function(result);
    REQUIRE_EQ(fn.parameters.size(), 2u);

    const auto& nested = get<ASTArrayType>(ast.type(*fn.parameters[0].type));
    CHECK(is<ASTArrayType>(ast.type(nested.element_type)));
    CHECK(is<ASTNameExpr>(ast.expression(nested.extent)));

    const auto& callback = get<ASTFunctionType>(ast.type(*fn.parameters[1].type));
    REQUIRE_EQ(callback.parameters.size(), 2u);
    CHECK_EQ(callback.parameters[0].access.mode, ASTAccessMode::Read);
    CHECK(!callback.parameters[0].access.marker.has_value());
    CHECK_EQ(callback.parameters[1].access.mode, ASTAccessMode::Write);
    REQUIRE(callback.parameters[1].access.marker.has_value());
    CHECK_EQ(slice(text, *callback.parameters[1].access.marker), "&");
    CHECK(is<ASTNamedType>(ast.type(callback.result_type)));
}

TEST_CASE("Parser: global external type roots survive nested arguments and casts") {
    constexpr auto text = std::string_view(
        "fn f(value: ::std::vector<::std::vector<i32>>) -> ::Point {"
        " return value as ::Point; }"
    );
    const auto tree = parse_valid(text);
    const auto ast = tree.view();
    const auto& declaration = function(tree);
    const auto& outer = get<ASTNamedType>(ast.type(*declaration.parameters[0].type));
    REQUIRE(outer.global_root.has_value());
    CHECK_EQ(slice(text, *outer.global_root), "::");
    REQUIRE_EQ(outer.arguments.size(), 1uz);
    const auto& inner = get<ASTNamedType>(ast.type(outer.arguments[0]));
    REQUIRE(inner.global_root.has_value());
    REQUIRE_EQ(inner.arguments.size(), 1uz);
    const auto& builtin = get<ASTNamedType>(ast.type(inner.arguments[0]));
    CHECK(!builtin.global_root.has_value());
}
