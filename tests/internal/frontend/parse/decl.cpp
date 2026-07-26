module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.decl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :source.text;
import :test.internal.frontend.parse.fixture;
import std;

TEST_CASE("Parser: module root separates imports from ordered top-level items") {
    static constexpr auto text = std::string_view(
        "import math.vector using { Vector, dot, };\n"
        "import .text using *;\n"
        "import logging::api.write using write;\n"
        "export enum State: u8 { Idle, Busy = 4, }\n"
        "struct Point { x: i32, y: i32, }\n"
        "private fn run(value: Point) -> i32 { return value.x; }\n"
        "private const answer: i32 = 42;\n"
        "#[cpp] { static_assert(true); }\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& hir_module = root(result);
    CHECK_EQ(hir_module.span.start(), 0u);
    CHECK_EQ(hir_module.span.end(), text.size());
    REQUIRE_EQ(hir_module.imports.size(), 3u);
    REQUIRE_EQ(hir_module.items.size(), 5u);

    const auto& first = ast.import_declaration(hir_module.imports[0]);
    REQUIRE(std::holds_alternative<ASTDomainRootModuleReference>(first.module_reference.value));
    const auto& first_reference =
        std::get<ASTDomainRootModuleReference>(first.module_reference.value);
    REQUIRE_EQ(first_reference.components.size(), 2u);
    CHECK_EQ(slice(text, first_reference.components[0]), "math");
    const auto& selected = get<ASTImportList>(first.selection);
    CHECK_EQ(selected.names.size(), 2u);
    CHECK(is<ASTWildcardImport>(ast.import_declaration(hir_module.imports[1]).selection));
    CHECK(is<ASTSingleImport>(ast.import_declaration(hir_module.imports[2]).selection));
    const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
        ast.import_declaration(hir_module.imports[2]).module_reference.value
    );
    CHECK_EQ(slice(text, qualified.name_span), "logging");
    CHECK_EQ(slice(text, qualified.separator_span), "::");
    REQUIRE_EQ(qualified.components.size(), 2u);
    CHECK_EQ(slice(text, qualified.components[0]), "api");
    CHECK_EQ(slice(text, qualified.components[1]), "write");

    const auto& enumeration = get<ASTEnumDecl>(ast.item(hir_module.items[0]));
    CHECK(std::holds_alternative<ASTExportDeclarationVisibility>(enumeration.visibility));
    CHECK_EQ(slice(text, enumeration.name_span), "State");
    REQUIRE(enumeration.underlying_type.has_value());
    CHECK(is<ASTNamedType>(ast.type(*enumeration.underlying_type)));
    REQUIRE_EQ(enumeration.cases.size(), 2u);
    CHECK(enumeration.cases[1].initializer.has_value());

    const auto& structure = get<ASTStructDecl>(ast.item(hir_module.items[1]));
    CHECK(std::holds_alternative<ASTBareDeclarationVisibility>(structure.visibility));
    const auto& function = get<ASTFunctionDecl>(ast.item(hir_module.items[2]));
    CHECK(std::holds_alternative<ASTPrivateDeclarationVisibility>(function.visibility));
    const auto& constant = get<ASTConstantDecl>(ast.item(hir_module.items[3]));
    CHECK(std::holds_alternative<ASTPrivateDeclarationVisibility>(constant.visibility));
    CHECK_EQ(
        slice(text, std::get<ASTPrivateDeclarationVisibility>(constant.visibility).keyword_span),
        "private"
    );
    CHECK_EQ(slice(text, constant.name_span), "answer");
    REQUIRE(constant.type.has_value());
    CHECK(is<ASTNamedType>(ast.type(*constant.type)));
    CHECK_EQ(slice(text, ast.expression(constant.initializer).span), "42");
    CHECK(is<CppRegion>(ast.item(hir_module.items[4])));
}

TEST_CASE("Parser: enum cases retain positional payload type lists") {
    static constexpr auto text =
        std::string_view("enum Shape { Circle(f64), Rect(f64, f64,), Point, Tagged(Item) = 4, }");
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& enumeration = get<ASTEnumDecl>(item(result, 0));
    REQUIRE_EQ(enumeration.cases.size(), 4u);
    CHECK_EQ(enumeration.cases[0].payload_types.size(), 1u);
    CHECK_EQ(enumeration.cases[1].payload_types.size(), 2u);
    CHECK(enumeration.cases[2].payload_types.empty());
    REQUIRE_EQ(enumeration.cases[3].payload_types.size(), 1u);
    CHECK(is<ASTNamedType>(ast.type(enumeration.cases[3].payload_types[0])));
    CHECK(enumeration.cases[3].initializer.has_value());

    check_invalid("enum EmptyPayload { Case() }");
    check_invalid("enum MissingPayload { Case(i32, )");
}

TEST_CASE("Parser: imports form one contiguous nonempty-selection prefix") {
    static constexpr auto invalid = std::to_array<std::string_view>({
        "import core;",
        "import core using;",
        "import core using {};",
        "import core using { first second };",
        "fn main() {} import late using *;",
        "#[cpp] {} import late using Name;",
        "export import core using *;",
        "import ::parser using parse;",
        "import json:: using parse;",
        "import . using parse;",
    });
    for (const auto& text : invalid) {
        check_invalid(text);
    }
}

TEST_CASE("Parser: test declarations use distinct typed items") {
    static constexpr auto text = std::string_view(
        "test \"empty\" {}\n"
        "test \"with body\" { let value = 1; }\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    REQUIRE_EQ(root(result).items.size(), 2u);
    const auto& declaration = get<ASTTestDecl>(item(result, 1));
    CHECK_EQ(slice(text, declaration.keyword_span), "test");
    CHECK_EQ(slice(text, declaration.name_span), "\"with body\"");
    CHECK_EQ(declaration.name, "with body");
    CHECK_EQ(ast.block(declaration.body).statements.size(), 1u);

    check_invalid("test name {}", "expected test name string");
    check_invalid("test {}", "expected test name string");
    check_invalid(
        "export test \"name\" {}",
        "expected enum, struct, function, or const after visibility modifier"
    );
    check_invalid("fn nested() { test \"name\" {} }");
}

TEST_CASE("Parser: module references retain their distinct source forms") {
    static constexpr auto text = std::string_view(
        "import model.user using User;\n"
        "import .sibling using Sibling;\n"
        "import json::parser.value using Value;\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& imports = root(result).imports;
    REQUIRE_EQ(imports.size(), 3u);

    const auto& domain_root = std::get<ASTDomainRootModuleReference>(
        ast.import_declaration(imports[0]).module_reference.value
    );
    REQUIRE_EQ(domain_root.components.size(), 2u);
    CHECK_EQ(slice(text, domain_root.components.front()), "model");

    const auto& relative = std::get<ASTParentRelativeModuleReference>(
        ast.import_declaration(imports[1]).module_reference.value
    );
    CHECK_EQ(slice(text, relative.prefix_span), ".");
    REQUIRE_EQ(relative.components.size(), 1u);
    CHECK_EQ(slice(text, relative.components.front()), "sibling");

    const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
        ast.import_declaration(imports[2]).module_reference.value
    );
    CHECK_EQ(slice(text, qualified.name_span), "json");
    CHECK_EQ(slice(text, qualified.separator_span), "::");
    REQUIRE_EQ(qualified.components.size(), 2u);
    CHECK_EQ(slice(text, qualified.components.back()), "value");
}

TEST_CASE("Parser: top-level constants require a name, initializer, and terminator") {
    static constexpr auto text = std::string_view(
        "private const hidden = 1;\n"
        "const local: i32 = 2;\n"
        "export const shared: u32 = 3u32;\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    REQUIRE_EQ(root(result).items.size(), 3u);

    const auto& hidden = get<ASTConstantDecl>(item(result, 0));
    CHECK(std::holds_alternative<ASTPrivateDeclarationVisibility>(hidden.visibility));
    CHECK(!hidden.type.has_value());
    const auto& local = get<ASTConstantDecl>(item(result, 1));
    CHECK(std::holds_alternative<ASTBareDeclarationVisibility>(local.visibility));
    REQUIRE(local.type.has_value());
    const auto& shared = get<ASTConstantDecl>(item(result, 2));
    CHECK(std::holds_alternative<ASTExportDeclarationVisibility>(shared.visibility));
    CHECK_EQ(
        slice(text, std::get<ASTExportDeclarationVisibility>(shared.visibility).keyword_span),
        "export"
    );
    REQUIRE(shared.type.has_value());
    CHECK_EQ(slice(text, ast.expression(shared.initializer).span), "3u32");

    check_invalid("const _ = 1;", "a top-level constant requires a named target");
    check_invalid("const missing;");
    check_invalid("const missing =;");
    check_invalid("const missing = 1");
}

TEST_CASE("Parser: declaration diagnostics reject malformed forms") {
    const auto empty_structure = parse_valid("struct Empty {}");
    const auto& structure = get<ASTStructDecl>(item(empty_structure, 0));
    CHECK(structure.fields.empty());

    static constexpr auto invalid = std::to_array<std::string_view>({
        "enum State {}",
        "fn missing_body();",
        "export #[cpp] {}",
        "private #[cpp] {}",
        "private test \"name\" {}",
        "private export fn invalid() {}",
        "export private fn invalid() {}",
        "enum E { A = }",
        "struct S { value i32 }",
    });
    for (const auto& text : invalid) {
        check_invalid(text);
    }
}

TEST_CASE("Parser: exact underscore is a discard parameter target") {
    static constexpr auto text = std::string_view("fn discard(_: i32, &: i32) {}");
    check_invalid(text);

    static constexpr auto valid_text =
        std::string_view("fn discard(_: i32, &_ : i32, _name: i32) {}");
    const auto result = parse_valid(valid_text);
    const auto& parameters = function(result).parameters;
    REQUIRE_EQ(parameters.size(), 3u);
    CHECK(is<ASTDiscardBindingTarget>(parameters[0].target));
    CHECK_EQ(parameters[0].access.mode, ASTAccessMode::Read);
    CHECK(!parameters[0].access.marker.has_value());
    CHECK(is<ASTDiscardBindingTarget>(parameters[1].target));
    CHECK_EQ(parameters[1].access.mode, ASTAccessMode::Write);
    REQUIRE(parameters[1].access.marker.has_value());
    CHECK_EQ(slice(valid_text, *parameters[1].access.marker), "&");
    CHECK(is<ASTNamedBindingTarget>(parameters[2].target));
    CHECK_EQ(
        slice(valid_text, get<ASTNamedBindingTarget>(parameters[2].target).name_span),
        "_name"
    );
}

TEST_CASE("Parser: callable parameters retain Read Write and Take access") {
    static constexpr auto text = std::string_view(
        "fn access(view: i32, &update: i32, &&take: i32, "
        "callback: fn(i32, &i32, &&i32) -> i32) { "
        "let closure = [](value: i32, &changed: i32, &&owned: i32) {}; }"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& parameters = function(result).parameters;

    REQUIRE_EQ(parameters.size(), 4u);
    CHECK_EQ(parameters[0].access.mode, ASTAccessMode::Read);
    CHECK_EQ(parameters[1].access.mode, ASTAccessMode::Write);
    CHECK_EQ(parameters[2].access.mode, ASTAccessMode::Take);

    const auto& function_type = get<ASTFunctionType>(ast.type(*parameters[3].type));
    REQUIRE_EQ(function_type.parameters.size(), 3u);
    CHECK_EQ(function_type.parameters[0].access.mode, ASTAccessMode::Read);
    CHECK_EQ(function_type.parameters[1].access.mode, ASTAccessMode::Write);
    CHECK_EQ(function_type.parameters[2].access.mode, ASTAccessMode::Take);

    const auto& body = ast.block(function(result).body);
    const auto& binding = get<ASTVariableDecl>(ast.statement(body.statements[0]));
    const auto& closure = get<ASTLambdaExpr>(ast.expression(*binding.initializer));
    REQUIRE_EQ(closure.parameters.size(), 3u);
    CHECK_EQ(closure.parameters[0].access.mode, ASTAccessMode::Read);
    CHECK_EQ(closure.parameters[1].access.mode, ASTAccessMode::Write);
    CHECK_EQ(closure.parameters[2].access.mode, ASTAccessMode::Take);

    check_invalid("fn invalid() { let closure = [&&value]() {}; }");
}
