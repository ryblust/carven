module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.decl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
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
        "#[cpp] ---\n"
        "static_assert(true);\n"
        "---\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& module_syntax = root(result);
    CHECK_EQ(module_syntax.span.start(), 0u);
    CHECK_EQ(module_syntax.span.end(), text.size());
    REQUIRE_EQ(module_syntax.module_imports.size(), 3u);
    REQUIRE(module_syntax.cpp_header_imports.empty());
    REQUIRE_EQ(module_syntax.cpp_source_fragments.size(), 1u);
    REQUIRE_EQ(module_syntax.items.size(), 4u);

    const auto& first = ast.module_import(module_syntax.module_imports[0]);
    REQUIRE(std::holds_alternative<ASTDomainRootModuleReference>(first.module_reference.value));
    const auto& first_reference =
        std::get<ASTDomainRootModuleReference>(first.module_reference.value);
    REQUIRE_EQ(first_reference.components.size(), 2u);
    CHECK_EQ(slice(text, first_reference.components[0]), "math");
    const auto& selected = get<ASTImportList>(first.selection);
    CHECK_EQ(selected.names.size(), 2u);
    CHECK(is<ASTWildcardImport>(ast.module_import(module_syntax.module_imports[1]).selection));
    CHECK(is<ASTSingleImport>(ast.module_import(module_syntax.module_imports[2]).selection));
    const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
        ast.module_import(module_syntax.module_imports[2]).module_reference.value
    );
    CHECK_EQ(slice(text, qualified.name_span), "logging");
    CHECK_EQ(slice(text, qualified.separator_span), "::");
    REQUIRE_EQ(qualified.components.size(), 2u);
    CHECK_EQ(slice(text, qualified.components[0]), "api");
    CHECK_EQ(slice(text, qualified.components[1]), "write");

    const auto& enumeration = get<ASTEnumDecl>(ast.item(module_syntax.items[0]));
    CHECK(std::holds_alternative<ASTExportDeclarationVisibility>(enumeration.visibility));
    CHECK_EQ(slice(text, enumeration.name_span), "State");
    REQUIRE(enumeration.underlying_type.has_value());
    CHECK(is<ASTNamedType>(ast.type(*enumeration.underlying_type)));
    REQUIRE_EQ(enumeration.cases.size(), 2u);
    CHECK(enumeration.cases[1].initializer.has_value());

    const auto& structure = get<ASTStructDecl>(ast.item(module_syntax.items[1]));
    CHECK(std::holds_alternative<ASTBareDeclarationVisibility>(structure.visibility));
    const auto& function = get<ASTFunctionDecl>(ast.item(module_syntax.items[2]));
    CHECK(std::holds_alternative<ASTPrivateDeclarationVisibility>(function.visibility));
    const auto& constant = get<ASTConstantDecl>(ast.item(module_syntax.items[3]));
    CHECK(std::holds_alternative<ASTPrivateDeclarationVisibility>(constant.visibility));
    CHECK_EQ(
        slice(text, std::get<ASTPrivateDeclarationVisibility>(constant.visibility).keyword_span),
        "private"
    );
    CHECK_EQ(slice(text, constant.name_span), "answer");
    REQUIRE(constant.type.has_value());
    CHECK(is<ASTNamedType>(ast.type(*constant.type)));
    CHECK_EQ(slice(text, ast.expression(constant.initializer).span), "42");
    const auto& fragment = module_syntax.cpp_source_fragments.front();
    CHECK_EQ(slice(text, fragment.payload_span), "static_assert(true);\n");
}

TEST_CASE("Parser: C++ headers, fragments, and function forms retain distinct structure") {
    static constexpr auto text = std::string_view(
        "import <cstdint>;\n"
        "import \"native/provider.hpp\";\n"
        "private import(cpp) fn native_value(value: i32) -> i32;\n"
        "import(cpp) fn domain_value() -> i32;\n"
        "export(cpp) fn value() -> i32 { return domain_value(); }\n"
        "#[cpp] ---\n"
        "static_assert(true);\n"
        "---\n"
    );
    const auto result = parse_valid(text);
    const auto& module_syntax = root(result);
    REQUIRE(module_syntax.module_imports.empty());
    REQUIRE_EQ(module_syntax.cpp_header_imports.size(), 2u);
    REQUIRE_EQ(module_syntax.cpp_source_fragments.size(), 1u);
    REQUIRE_EQ(module_syntax.items.size(), 3u);

    const auto& angle = module_syntax.cpp_header_imports[0];
    CHECK_EQ(slice(text, angle.span), "import <cstdint>;");
    CHECK_EQ(angle.delimiter, ASTCppHeaderDelimiter::AngleBrackets);
    CHECK_EQ(slice(text, angle.name_span), "cstdint");
    const auto& quote = module_syntax.cpp_header_imports[1];
    CHECK_EQ(slice(text, quote.span), "import \"native/provider.hpp\";");
    CHECK_EQ(quote.delimiter, ASTCppHeaderDelimiter::Quotes);
    CHECK_EQ(slice(text, quote.name_span), "native/provider.hpp");

    const auto& private_import = function(result, 0);
    CHECK(is<ASTPrivateDeclarationVisibility>(private_import.visibility));
    CHECK(is<ASTCppImportForm>(private_import.implementation));
    CHECK(!private_import.cpp_export.has_value());

    const auto& bare_import = function(result, 1);
    CHECK(is<ASTBareDeclarationVisibility>(bare_import.visibility));
    CHECK(is<ASTCppImportForm>(bare_import.implementation));

    const auto& cpp_export = function(result, 2);
    CHECK(is<ASTExportDeclarationVisibility>(cpp_export.visibility));
    REQUIRE(cpp_export.cpp_export.has_value());
    CHECK_EQ(slice(text, cpp_export.cpp_export->span), "export(cpp)");
    CHECK(is<ASTFunctionBody>(cpp_export.implementation));

    const auto& fragment = module_syntax.cpp_source_fragments.front();
    CHECK_EQ(slice(text, fragment.form_span), "#[cpp] ---\nstatic_assert(true);\n---");
    CHECK_EQ(slice(text, fragment.payload_span), "static_assert(true);\n");
}

TEST_CASE("Parser: module and C++ header imports keep independent ownership") {
    static constexpr auto text = std::string_view(
        "import .first using First;\n"
        "import <native/first.hpp>;\n"
        "import .second using Second;\n"
        "import \"native/second.hpp\";\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& module_syntax = root(result);
    REQUIRE_EQ(module_syntax.module_imports.size(), 2u);
    REQUIRE_EQ(module_syntax.cpp_header_imports.size(), 2u);

    CHECK_EQ(
        slice(text, ast.module_import(module_syntax.module_imports[0]).span),
        "import .first using First;"
    );
    CHECK_EQ(
        slice(text, ast.module_import(module_syntax.module_imports[1]).span),
        "import .second using Second;"
    );
    CHECK_EQ(slice(text, module_syntax.cpp_header_imports[0].span), "import <native/first.hpp>;");
    CHECK_EQ(slice(text, module_syntax.cpp_header_imports[1].span), "import \"native/second.hpp\";");
}

TEST_CASE("Parser: C++ source fragments remain independent module-owned spans") {
    static constexpr auto text = std::string_view(
        "#[cpp] ---\n"
        "first();\n"
        "---\n"
        "fn value() {}\n"
        "#[cpp] -----\n"
        "auto raw = R\"(---)\";\n"
        "-----\n"
        "#[cpp] ---\n"
        "---\n"
    );
    const auto result = parse_valid(text);
    const auto& module_syntax = root(result);
    REQUIRE_EQ(module_syntax.items.size(), 1u);
    REQUIRE_EQ(module_syntax.cpp_source_fragments.size(), 3u);
    CHECK_EQ(slice(text, module_syntax.cpp_source_fragments[0].payload_span), "first();\n");
    CHECK_EQ(slice(text, module_syntax.cpp_source_fragments[1].payload_span), "auto raw = R\"(---)\";\n");
    CHECK(module_syntax.cpp_source_fragments[2].payload_span.empty());

    check_invalid("#[cpp] ---\n---\n;");
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
        "#[cpp] ---\n---\nimport late using Name;",
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
    const auto& imports = root(result).module_imports;
    REQUIRE_EQ(imports.size(), 3u);

    const auto& domain_root = std::get<ASTDomainRootModuleReference>(
        ast.module_import(imports[0]).module_reference.value
    );
    REQUIRE_EQ(domain_root.components.size(), 2u);
    CHECK_EQ(slice(text, domain_root.components.front()), "model");

    const auto& relative = std::get<ASTParentRelativeModuleReference>(
        ast.module_import(imports[1]).module_reference.value
    );
    CHECK_EQ(slice(text, relative.prefix_span), ".");
    REQUIRE_EQ(relative.components.size(), 1u);
    CHECK_EQ(slice(text, relative.components.front()), "sibling");

    const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
        ast.module_import(imports[2]).module_reference.value
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
    const auto empty_enumeration = parse_valid("enum State {}");
    const auto& enumeration = get<ASTEnumDecl>(item(empty_enumeration, 0));
    CHECK(enumeration.cases.empty());

    static constexpr auto invalid = std::to_array<std::string_view>({
        "fn missing_body();",
        "import(cpp) fn invalid() {}",
        "private import(cpp) const invalid = 1;",
        "export #[cpp] ---\n---",
        "private #[cpp] ---\n---",
        "private test \"name\" {}",
        "private export fn invalid() {}",
        "export private fn invalid() {}",
        "enum E { A = }",
        "struct S { value i32 }",
    });
    for (const auto& text : invalid) {
        check_invalid(text);
    }
    check_invalid(
        "export import(cpp) fn invalid();",
        "an import(cpp) declaration cannot be exported directly"
    );
    check_invalid(
        "export(cpp) import(cpp) fn invalid();",
        "an import(cpp) declaration cannot be exported directly"
    );
    check_invalid(
        "import(cpp) export(cpp) fn invalid();",
        "import(cpp) and export(cpp) forms must introduce a function"
    );
    check_invalid(
        "private import(cpp) export(cpp) fn invalid();",
        "import(cpp) and export(cpp) forms must introduce a function"
    );
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

    const auto& body = function_body(result);
    const auto& binding = get<ASTVariableDecl>(ast.statement(body.statements[0]));
    const auto& closure = get<ASTLambdaExpr>(ast.expression(*binding.initializer));
    REQUIRE_EQ(closure.parameters.size(), 3u);
    CHECK_EQ(closure.parameters[0].access.mode, ASTAccessMode::Read);
    CHECK_EQ(closure.parameters[1].access.mode, ASTAccessMode::Write);
    CHECK_EQ(closure.parameters[2].access.mode, ASTAccessMode::Take);

    check_invalid("fn invalid() { let closure = [&&value]() {}; }");
}

TEST_CASE("Parser: C++ selections retain qualified names and explicit namespaces") {
    constexpr auto text = std::string_view(
        "import <vector> using std::vector;\n"
        "import \"provider.hpp\" using { vendor::Widget, vendor::create, };\n"
        "import <utility> using std::*;\n"
    );
    const auto tree = parse_valid(text);
    const auto& imports = root(tree).cpp_header_imports;
    REQUIRE_EQ(imports.size(), 3uz);
    REQUIRE_EQ(imports[0].bindings.size(), 1uz);
    CHECK_FALSE(imports[0].bindings[0].opens_namespace);
    REQUIRE_EQ(imports[0].bindings[0].components.size(), 2uz);
    CHECK_EQ(slice(text, imports[0].bindings[0].components.back()), "vector");
    CHECK_EQ(imports[1].bindings.size(), 2uz);
    CHECK(imports[2].bindings[0].opens_namespace);
    const auto invalid = std::array {
        "import <vector> using *;",
        "import <vector> using {};",
        "import <vector> using std::;",
        "import <vector> using {std::*,};",
    };
    for (const auto* const source : invalid) {
        check_invalid(source);
    }
}
