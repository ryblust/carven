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
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Parser declaration: const functions retain their qualifier and ordinary callable bodies",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "const fn twice(value: i32) -> i32 => value * 2;\n"
                "private const fn label() -> String { return f\"answer {42}\"; }\n"
                "export const fn exported(value: u32) -> u32 { return value; }\n"
                "const answer = twice(21);\n"
                "fn ordinary() -> i32 => 42;\n"
            );
            const auto result = parse_valid(text);
            for (const auto index : {0uz, 1uz, 2uz}) {
                const auto& declaration = function(result, index);
                if (!ct::expect(declaration.const_span.has_value())) {
                    return;
                }
                ct::expect_equal(slice(text, *declaration.const_span), std::string_view("const"));
                ct::expect(is<ASTFunctionBody>(declaration.implementation));
            }
            ct::expect(is<ASTPrivateDeclarationVisibility>(function(result, 1).visibility));
            ct::expect(is<ASTExportDeclarationVisibility>(function(result, 2).visibility));
            ct::expect(is<ASTConstantDecl>(result.view().item(root(result).items[3])));
            ct::expect(!(function(result, 4).const_span.has_value()));
        }
    );

    ct::test(
        "Parser declaration: module root separates imports from ordered top-level items",
        [] static noexcept {
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
            ct::expect_equal(module_syntax.span.start(), 0u);
            ct::expect_equal(module_syntax.span.end(), text.size());
            if (!ct::expect_equal(module_syntax.module_imports.size(), 3uz)) {
                return;
            }
            if (!ct::expect(module_syntax.cpp_header_imports.empty())) {
                return;
            }
            if (!ct::expect_equal(module_syntax.cpp_source_fragments.size(), 1uz)) {
                return;
            }
            if (!ct::expect_equal(module_syntax.items.size(), 4uz)) {
                return;
            }

            const auto& first = ast.module_import(module_syntax.module_imports[0]);
            if (!ct::expect(
                    std::holds_alternative<ASTDomainRootModuleReference>(
                        first.module_reference.value
                    )
                )) {
                return;
            }
            const auto& first_reference =
                std::get<ASTDomainRootModuleReference>(first.module_reference.value);
            if (!ct::expect_equal(first_reference.components.size(), 2uz)) {
                return;
            }
            ct::expect_equal(slice(text, first_reference.components[0]), std::string_view("math"));
            const auto& selected = get<ASTImportList>(first.selection);
            ct::expect_equal(selected.names.size(), 2uz);
            ct::expect(
                is<ASTWildcardImport>(ast.module_import(module_syntax.module_imports[1]).selection)
            );
            ct::expect(
                is<ASTSingleImport>(ast.module_import(module_syntax.module_imports[2]).selection)
            );
            const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
                ast.module_import(module_syntax.module_imports[2]).module_reference.value
            );
            ct::expect_equal(slice(text, qualified.name_span), std::string_view("logging"));
            ct::expect_equal(slice(text, qualified.separator_span), std::string_view("::"));
            if (!ct::expect_equal(qualified.components.size(), 2uz)) {
                return;
            }
            ct::expect_equal(slice(text, qualified.components[0]), std::string_view("api"));
            ct::expect_equal(slice(text, qualified.components[1]), std::string_view("write"));

            const auto& enumeration = get<ASTEnumDecl>(ast.item(module_syntax.items[0]));
            ct::expect(
                std::holds_alternative<ASTExportDeclarationVisibility>(enumeration.visibility)
            );
            ct::expect_equal(slice(text, enumeration.name_span), std::string_view("State"));
            if (!ct::expect(enumeration.underlying_type.has_value())) {
                return;
            }
            ct::expect(is<ASTNamedType>(ast.type(*enumeration.underlying_type)));
            if (!ct::expect_equal(enumeration.cases.size(), 2uz)) {
                return;
            }
            ct::expect(enumeration.cases[1].initializer.has_value());

            const auto& structure = get<ASTRecordDecl>(ast.item(module_syntax.items[1]));
            ct::expect(std::holds_alternative<ASTBareDeclarationVisibility>(structure.visibility));
            const auto& function = get<ASTFunctionDecl>(ast.item(module_syntax.items[2]));
            ct::expect(
                std::holds_alternative<ASTPrivateDeclarationVisibility>(function.visibility)
            );
            const auto& constant = get<ASTConstantDecl>(ast.item(module_syntax.items[3]));
            ct::expect(
                std::holds_alternative<ASTPrivateDeclarationVisibility>(constant.visibility)
            );
            ct::expect_equal(
                slice(
                    text,
                    std::get<ASTPrivateDeclarationVisibility>(constant.visibility).keyword_span
                ),
                std::string_view("private")
            );
            ct::expect_equal(slice(text, constant.name_span), std::string_view("answer"));
            if (!ct::expect(constant.type.has_value())) {
                return;
            }
            ct::expect(is<ASTNamedType>(ast.type(*constant.type)));
            ct::expect_equal(
                slice(text, ast.expression(constant.initializer).span),
                std::string_view("42")
            );
            const auto& fragment = module_syntax.cpp_source_fragments.front();
            ct::expect_equal(
                slice(text, fragment.payload_span),
                std::string_view("static_assert(true);\n")
            );
        }
    );

    ct::test(
        "Parser: C++ headers, fragments, and function forms retain distinct structure",
        [] static noexcept {
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
            if (!ct::expect(module_syntax.module_imports.empty())) {
                return;
            }
            if (!ct::expect_equal(module_syntax.cpp_header_imports.size(), 2uz)) {
                return;
            }
            if (!ct::expect_equal(module_syntax.cpp_source_fragments.size(), 1uz)) {
                return;
            }
            if (!ct::expect_equal(module_syntax.items.size(), 3uz)) {
                return;
            }

            const auto& angle = module_syntax.cpp_header_imports[0];
            ct::expect_equal(slice(text, angle.span), std::string_view("import <cstdint>;"));
            ct::expect_equal(angle.delimiter, ASTCppHeaderDelimiter::AngleBrackets);
            ct::expect_equal(slice(text, angle.name_span), std::string_view("cstdint"));
            const auto& quote = module_syntax.cpp_header_imports[1];
            ct::expect_equal(
                slice(text, quote.span),
                std::string_view("import \"native/provider.hpp\";")
            );
            ct::expect_equal(quote.delimiter, ASTCppHeaderDelimiter::Quotes);
            ct::expect_equal(slice(text, quote.name_span), std::string_view("native/provider.hpp"));

            const auto& private_import = function(result, 0);
            ct::expect(is<ASTPrivateDeclarationVisibility>(private_import.visibility));
            ct::expect(is<ASTCppImportForm>(private_import.implementation));
            ct::expect(!private_import.cpp_export.has_value());

            const auto& bare_import = function(result, 1);
            ct::expect(is<ASTBareDeclarationVisibility>(bare_import.visibility));
            ct::expect(is<ASTCppImportForm>(bare_import.implementation));

            const auto& cpp_export = function(result, 2);
            ct::expect(is<ASTExportDeclarationVisibility>(cpp_export.visibility));
            if (!ct::expect(cpp_export.cpp_export.has_value())) {
                return;
            }
            ct::expect_equal(
                slice(text, cpp_export.cpp_export->span),
                std::string_view("export(cpp)")
            );
            ct::expect(is<ASTFunctionBody>(cpp_export.implementation));

            const auto& fragment = module_syntax.cpp_source_fragments.front();
            ct::expect_equal(
                slice(text, fragment.form_span),
                std::string_view("#[cpp] ---\nstatic_assert(true);\n---")
            );
            ct::expect_equal(
                slice(text, fragment.payload_span),
                std::string_view("static_assert(true);\n")
            );
        }
    );

    ct::test(
        "Parser declaration: module and C++ header imports keep independent ownership",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "import .first using First;\n"
                "import <native/first.hpp>;\n"
                "import .second using Second;\n"
                "import \"native/second.hpp\";\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& module_syntax = root(result);
            if (!ct::expect_equal(module_syntax.module_imports.size(), 2uz)) {
                return;
            }
            if (!ct::expect_equal(module_syntax.cpp_header_imports.size(), 2uz)) {
                return;
            }

            ct::expect_equal(
                slice(text, ast.module_import(module_syntax.module_imports[0]).span),
                std::string_view("import .first using First;")
            );
            ct::expect_equal(
                slice(text, ast.module_import(module_syntax.module_imports[1]).span),
                std::string_view("import .second using Second;")
            );
            ct::expect_equal(
                slice(text, module_syntax.cpp_header_imports[0].span),
                std::string_view("import <native/first.hpp>;")
            );
            ct::expect_equal(
                slice(text, module_syntax.cpp_header_imports[1].span),
                std::string_view("import \"native/second.hpp\";")
            );
        }
    );

    ct::test(
        "Parser declaration: C++ source fragments remain independent module-owned spans",
        [] static noexcept {
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
            if (!ct::expect_equal(module_syntax.items.size(), 1uz)) {
                return;
            }
            if (!ct::expect_equal(module_syntax.cpp_source_fragments.size(), 3uz)) {
                return;
            }
            ct::expect_equal(
                slice(text, module_syntax.cpp_source_fragments[0].payload_span),
                std::string_view("first();\n")
            );
            ct::expect_equal(
                slice(text, module_syntax.cpp_source_fragments[1].payload_span),
                std::string_view("auto raw = R\"(---)\";\n")
            );
            ct::expect(module_syntax.cpp_source_fragments[2].payload_span.empty());

            check_invalid("#[cpp] ---\n---\n;");
        }
    );

    ct::test(
        "Parser declaration: enum cases retain positional payload type lists",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "enum Shape { Circle(f64), Rect(f64, f64,), Point, Tagged(Item) = 4, }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& enumeration = get<ASTEnumDecl>(item(result, 0));
            if (!ct::expect_equal(enumeration.cases.size(), 4uz)) {
                return;
            }
            ct::expect_equal(enumeration.cases[0].payload_types.size(), 1uz);
            ct::expect_equal(enumeration.cases[1].payload_types.size(), 2uz);
            ct::expect(enumeration.cases[2].payload_types.empty());
            if (!ct::expect_equal(enumeration.cases[3].payload_types.size(), 1uz)) {
                return;
            }
            ct::expect(is<ASTNamedType>(ast.type(enumeration.cases[3].payload_types[0])));
            ct::expect(enumeration.cases[3].initializer.has_value());

            check_invalid("enum EmptyPayload { Case() }");
            check_invalid("enum MissingPayload { Case(i32, )");
        }
    );

    ct::test(
        "Parser declaration: imports form one contiguous nonempty-selection prefix",
        [] static noexcept {
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
            ct::each(invalid, std::identity {}, [](const auto& text) static noexcept {
                check_invalid(text);
            });
        }
    );

    ct::test("Parser declaration: test declarations use distinct typed items", [] static noexcept {
        static constexpr auto text = std::string_view(
            "test {}\n"
            "test {}\n"
            "test \"\" {}\n"
            "test \"with body\" { let value = 1; }\n"
            "const test {}\n"
            "const test \"compile\" {}\n"
        );
        const auto result = parse_valid(text);
        const auto ast = result.view();
        if (!ct::expect_equal(root(result).items.size(), 6uz)) {
            return;
        }
        ct::expect(!(get<ASTTestDecl>(item(result, 0)).label.has_value()));
        ct::expect(!(get<ASTTestDecl>(item(result, 1)).label.has_value()));
        const auto& empty = get<ASTTestDecl>(item(result, 2));
        if (!ct::expect(empty.label.has_value())) {
            return;
        }
        ct::expect_equal(slice(text, empty.label->span), std::string_view("\"\""));
        ct::expect(empty.label->text.empty());
        const auto& declaration = get<ASTTestDecl>(item(result, 3));
        ct::expect_equal(slice(text, declaration.keyword_span), std::string_view("test"));
        if (!ct::expect(declaration.label.has_value())) {
            return;
        }
        ct::expect_equal(slice(text, declaration.label->span), std::string_view("\"with body\""));
        ct::expect_equal(declaration.label->text, std::string_view("with body"));
        ct::expect_equal(ast.block(declaration.body).statements.size(), 1uz);
        ct::expect(get<ASTTestDecl>(item(result, 4)).is_const);
        ct::expect(!(get<ASTTestDecl>(item(result, 4)).label.has_value()));
        const auto& compile = get<ASTTestDecl>(item(result, 5));
        ct::expect(compile.is_const);
        if (!ct::expect(compile.label.has_value())) {
            return;
        }
        ct::expect_equal(compile.label->text, std::string_view("compile"));

        check_invalid("test name {}", "expected '{'");
        check_invalid("test 42 {}", "expected '{'");
        check_invalid("test \"label\";", "expected '{'");
        check_invalid("const test name {}", "expected '{'");
        check_invalid(
            "export test \"name\" {}",
            "expected enum, struct, function, or const after visibility modifier"
        );
        check_invalid("fn nested() { test \"name\" {} }");
    });

    ct::test(
        "Parser declaration: module references retain their distinct source forms",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "import model.user using User;\n"
                "import .sibling using Sibling;\n"
                "import json::parser.value using Value;\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& imports = root(result).module_imports;
            if (!ct::expect_equal(imports.size(), 3uz)) {
                return;
            }

            const auto& domain_root = std::get<ASTDomainRootModuleReference>(
                ast.module_import(imports[0]).module_reference.value
            );
            if (!ct::expect_equal(domain_root.components.size(), 2uz)) {
                return;
            }
            ct::expect_equal(
                slice(text, domain_root.components.front()),
                std::string_view("model")
            );

            const auto& relative = std::get<ASTParentRelativeModuleReference>(
                ast.module_import(imports[1]).module_reference.value
            );
            ct::expect_equal(slice(text, relative.prefix_span), std::string_view("."));
            if (!ct::expect_equal(relative.components.size(), 1uz)) {
                return;
            }
            ct::expect_equal(slice(text, relative.components.front()), std::string_view("sibling"));

            const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
                ast.module_import(imports[2]).module_reference.value
            );
            ct::expect_equal(slice(text, qualified.name_span), std::string_view("json"));
            ct::expect_equal(slice(text, qualified.separator_span), std::string_view("::"));
            if (!ct::expect_equal(qualified.components.size(), 2uz)) {
                return;
            }
            ct::expect_equal(slice(text, qualified.components.back()), std::string_view("value"));
        }
    );

    ct::test(
        "Parser: top-level constants require a name, initializer, and terminator",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "private const hidden = 1;\n"
                "const local: i32 = 2;\n"
                "export const shared: u32 = 3u32;\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            if (!ct::expect_equal(root(result).items.size(), 3uz)) {
                return;
            }

            const auto& hidden = get<ASTConstantDecl>(item(result, 0));
            ct::expect(std::holds_alternative<ASTPrivateDeclarationVisibility>(hidden.visibility));
            ct::expect(!hidden.type.has_value());
            const auto& local = get<ASTConstantDecl>(item(result, 1));
            ct::expect(std::holds_alternative<ASTBareDeclarationVisibility>(local.visibility));
            if (!ct::expect(local.type.has_value())) {
                return;
            }
            const auto& shared = get<ASTConstantDecl>(item(result, 2));
            ct::expect(std::holds_alternative<ASTExportDeclarationVisibility>(shared.visibility));
            ct::expect_equal(
                slice(
                    text,
                    std::get<ASTExportDeclarationVisibility>(shared.visibility).keyword_span
                ),
                std::string_view("export")
            );
            if (!ct::expect(shared.type.has_value())) {
                return;
            }
            ct::expect_equal(
                slice(text, ast.expression(shared.initializer).span),
                std::string_view("3u32")
            );

            check_invalid("const _ = 1;", "a top-level constant requires a named target");
            check_invalid("const missing;");
            check_invalid("const missing =;");
            check_invalid("const missing = 1");
        }
    );

    ct::test(
        "Parser declaration: declaration diagnostics reject malformed forms",
        [] static noexcept {
            const auto empty_structure = parse_valid("struct Empty {}");
            const auto& structure = get<ASTRecordDecl>(item(empty_structure, 0));
            ct::expect(structure.fields.empty());
            const auto empty_enumeration = parse_valid("enum State {}");
            const auto& enumeration = get<ASTEnumDecl>(item(empty_enumeration, 0));
            ct::expect(enumeration.cases.empty());

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
            ct::each(invalid, std::identity {}, [](const auto& text) static noexcept {
                check_invalid(text);
            });
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
    );

    ct::test(
        "Parser declaration: exact underscore is a discard parameter target",
        [] static noexcept {
            static constexpr auto text = std::string_view("fn discard(_: i32, &: i32) {}");
            check_invalid(text);

            static constexpr auto valid_text =
                std::string_view("fn discard(_: i32, &_ : i32, _name: i32) {}");
            const auto result = parse_valid(valid_text);
            const auto& parameters = function(result).parameters;
            if (!ct::expect_equal(parameters.size(), 3uz)) {
                return;
            }
            ct::expect(is<ASTDiscardBindingTarget>(parameters[0].target));
            ct::expect_equal(parameters[0].access.mode, ASTAccessMode::Read);
            ct::expect(!parameters[0].access.marker.has_value());
            ct::expect(is<ASTDiscardBindingTarget>(parameters[1].target));
            ct::expect_equal(parameters[1].access.mode, ASTAccessMode::Write);
            if (!ct::expect(parameters[1].access.marker.has_value())) {
                return;
            }
            ct::expect_equal(
                slice(valid_text, *parameters[1].access.marker),
                std::string_view("&")
            );
            ct::expect(is<ASTNamedBindingTarget>(parameters[2].target));
            ct::expect_equal(
                slice(valid_text, get<ASTNamedBindingTarget>(parameters[2].target).name_span),
                std::string_view("_name")
            );
        }
    );

    ct::test(
        "Parser declaration: callable parameters retain Read Write and Take access",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn access(view: i32, &update: i32, &&take: i32, "
                "callback: fn(i32, &i32, &&i32) -> i32) { "
                "let closure = [](value: i32, &changed: i32, &&owned: i32) {}; }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& parameters = function(result).parameters;

            if (!ct::expect_equal(parameters.size(), 4uz)) {
                return;
            }
            ct::expect_equal(parameters[0].access.mode, ASTAccessMode::Read);
            ct::expect_equal(parameters[1].access.mode, ASTAccessMode::Write);
            ct::expect_equal(parameters[2].access.mode, ASTAccessMode::Take);

            const auto& function_type = get<ASTFunctionType>(ast.type(*parameters[3].type));
            if (!ct::expect_equal(function_type.parameters.size(), 3uz)) {
                return;
            }
            ct::expect_equal(function_type.parameters[0].access.mode, ASTAccessMode::Read);
            ct::expect_equal(function_type.parameters[1].access.mode, ASTAccessMode::Write);
            ct::expect_equal(function_type.parameters[2].access.mode, ASTAccessMode::Take);

            const auto& body = function_body(result);
            const auto& binding = get<ASTVariableDecl>(ast.statement(body.statements[0]));
            const auto& closure = get<ASTLambdaExpr>(ast.expression(*binding.initializer));
            if (!ct::expect_equal(closure.parameters.size(), 3uz)) {
                return;
            }
            ct::expect_equal(closure.parameters[0].access.mode, ASTAccessMode::Read);
            ct::expect_equal(closure.parameters[1].access.mode, ASTAccessMode::Write);
            ct::expect_equal(closure.parameters[2].access.mode, ASTAccessMode::Take);

            check_invalid("fn invalid() { let closure = [&&value]() {}; }");
        }
    );

    ct::test(
        "Parser declaration: C++ selections retain qualified names and explicit namespaces",
        [] static noexcept {
            constexpr auto text = std::string_view(
                "import <vector> using std::vector;\n"
                "import \"provider.hpp\" using vendor::{ Widget, create, };\n"
                "import <utility> using std::*;\n"
            );
            const auto tree = parse_valid(text);
            const auto& imports = root(tree).cpp_header_imports;
            if (!ct::expect_equal(imports.size(), 3uz)) {
                return;
            }
            if (!ct::expect(imports[0].using_clause.has_value())) {
                return;
            }
            const auto& first = *imports[0].using_clause;
            if (!ct::expect_equal(first.prefix.size(), 1uz)) {
                return;
            }
            ct::expect_equal(slice(text, first.prefix.front()), std::string_view("std"));
            ct::expect_equal(
                slice(text, std::get<ASTCppSingleSelection>(first.selection).name),
                std::string_view("vector")
            );
            ct::expect_equal(
                std::get<ASTCppListSelection>(imports[1].using_clause->selection).names.size(),
                2uz
            );
            ct::expect(
                std::holds_alternative<ASTCppNamespaceSelection>(imports[2].using_clause->selection)
            );
            const auto invalid = std::array {
                "import <vector> using *;",
                "import <vector> using {};",
                "import <vector> using std::;",
                "import <vector> using {std::*,};",
                "import <vector> using {std::vector};",
                "import <vector> using std::{nested::vector};",
                "import <vector> using std::{nested::{vector}};",
                "import <vector> using std::{vector, *};",
                "import <vector> using std::vector as v;",
            };
            ct::each(
                invalid,
                [](const char* source) static noexcept -> std::string_view { return source; },
                [](const char* source) static noexcept { check_invalid(source); }
            );
        }
    );

    ct::test(
        "Parser declaration: module components accept keyword spellings in every reference form",
        [] static noexcept {
            const auto cases = std::to_array<std::string_view>({
                "import using using *;",
                "import .import.export using value;",
                "import match::using.true using {value};",
            });
            ct::each(
                cases,
                [](std::string_view text) static noexcept -> std::string_view { return text; },
                [](std::string_view text) static noexcept {
                    const auto tree = parse_valid(text);
                    ct::expect_equal(root(tree).module_imports.size(), 1uz);
                }
            );
        }
    );

    ct::test(
        "Parser declaration: callable expression bodies retain syntax and outer delimiters",
        [] static noexcept {
            constexpr auto source = std::string_view(
                "fn add(a: i32) -> i32 => a + 1;\n"
                "fn nested() => [](a: i32) => [](b: i32) => a + b;\n"
                "fn use() { invoke([](a) => a * 2, 3); }\n"
            );
            const auto tree = parse_valid(source);
            const auto ast = tree.view();
            const auto& body =
                get<ASTExpressionBody>(get<ASTFunctionBody>(function(tree, 0).implementation).body);
            ct::expect_equal(slice(source, body.arrow_span), std::string_view("=>"));
            ct::expect(is<ASTBinaryExpr>(ast.expression(body.expression).value));
            const auto& outer =
                get<ASTExpressionBody>(get<ASTFunctionBody>(function(tree, 1).implementation).body);
            const auto& lambda = get<ASTLambdaExpr>(ast.expression(outer.expression).value);
            ct::expect(is<ASTExpressionBody>(lambda.body));
            const auto invalid = std::to_array<std::string_view>({
                "fn missing() => ;",
                "fn missing() => 1",
                "import(cpp) fn invalid() => 1;",
                "fn invalid() { let f = []() => ; }",
                "fn invalid() => let x = 1;",
            });
            ct::each(invalid, std::identity {}, [](const auto& text) static noexcept {
                check_invalid(text);
            });
        }
    );

    ct::test(
        "Parser declaration: top-level statements form one source-located implicit entry",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "let value = twice(21);\n"
                "fn twice(value: i32) => value * 2;\n"
                "const expected = 42;\n"
                "println(value);\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            if (!ct::expect_equal(root(result).items.size(), 3uz)) {
                return;
            }
            ct::expect(!(function(result, 0).is_implicit_entry));
            ct::expect(is<ASTConstantDecl>(ast.item(root(result).items[1])));
            const auto& entry = function(result, 2);
            if (!ct::expect(entry.is_implicit_entry)) {
                return;
            }
            ct::expect(entry.parameters.empty());
            const auto& implementation = std::get<ASTFunctionBody>(entry.implementation);
            const auto& body = ast.block(std::get<ASTBlockID>(implementation.body));
            if (!ct::expect_equal(body.statements.size(), 2uz)) {
                return;
            }
            ct::expect_equal(
                slice(text, ast.statement(body.statements[0]).span),
                std::string_view("let value = twice(21);")
            );
            ct::expect_equal(
                slice(text, ast.statement(body.statements[1]).span),
                std::string_view("println(value);")
            );
        }
    );
});

} // namespace
