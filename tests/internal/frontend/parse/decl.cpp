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

const TestSuite suite([] static noexcept {
    "Parser declaration: const functions retain their qualifier and ordinary callable bodies"_test =
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
                if (!expect(declaration.const_span.has_value())) {
                    return;
                }
                expect_equal(slice(text, *declaration.const_span), std::string_view("const"));
                expect(is<ASTFunctionBody>(declaration.implementation));
            }
            expect(is<ASTPrivateDeclarationVisibility>(function(result, 1).visibility));
            expect(is<ASTExportDeclarationVisibility>(function(result, 2).visibility));
            expect(is<ASTConstantDecl>(result.view().item(root(result).items[3])));
            expect(!(function(result, 4).const_span.has_value()));
        };

    "Parser declaration: module root separates imports from ordered top-level items"_test =
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
            expect_equal(module_syntax.span.start(), 0u);
            expect_equal(module_syntax.span.end(), text.size());
            if (!expect_equal(module_syntax.module_imports.size(), 3uz)) {
                return;
            }
            if (!expect(module_syntax.cpp_header_imports.empty())) {
                return;
            }
            if (!expect_equal(module_syntax.cpp_source_fragments.size(), 1uz)) {
                return;
            }
            if (!expect_equal(module_syntax.items.size(), 4uz)) {
                return;
            }

            const auto& first = ast.module_import(module_syntax.module_imports[0]);
            if (!expect(
                    std::holds_alternative<ASTDomainRootModuleReference>(
                        first.module_reference.value
                    )
                )) {
                return;
            }
            const auto& first_reference =
                std::get<ASTDomainRootModuleReference>(first.module_reference.value);
            if (!expect_equal(first_reference.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, first_reference.components[0]), std::string_view("math"));
            const auto& selected = get<ASTImportList>(first.selection);
            expect_equal(selected.names.size(), 2uz);
            expect(
                is<ASTWildcardImport>(ast.module_import(module_syntax.module_imports[1]).selection)
            );
            expect(
                is<ASTSingleImport>(ast.module_import(module_syntax.module_imports[2]).selection)
            );
            const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
                ast.module_import(module_syntax.module_imports[2]).module_reference.value
            );
            expect_equal(slice(text, qualified.name_span), std::string_view("logging"));
            expect_equal(slice(text, qualified.separator_span), std::string_view("::"));
            if (!expect_equal(qualified.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, qualified.components[0]), std::string_view("api"));
            expect_equal(slice(text, qualified.components[1]), std::string_view("write"));

            const auto& enumeration = get<ASTEnumDecl>(ast.item(module_syntax.items[0]));
            expect(std::holds_alternative<ASTExportDeclarationVisibility>(enumeration.visibility));
            expect_equal(slice(text, enumeration.name_span), std::string_view("State"));
            if (!expect(enumeration.underlying_type.has_value())) {
                return;
            }
            expect(is<ASTNamedType>(ast.type(*enumeration.underlying_type)));
            if (!expect_equal(enumeration.cases.size(), 2uz)) {
                return;
            }
            expect(enumeration.cases[1].initializer.has_value());

            const auto& structure = get<ASTRecordDecl>(ast.item(module_syntax.items[1]));
            expect(std::holds_alternative<ASTBareDeclarationVisibility>(structure.visibility));
            const auto& function = get<ASTFunctionDecl>(ast.item(module_syntax.items[2]));
            expect(std::holds_alternative<ASTPrivateDeclarationVisibility>(function.visibility));
            const auto& constant = get<ASTConstantDecl>(ast.item(module_syntax.items[3]));
            expect(std::holds_alternative<ASTPrivateDeclarationVisibility>(constant.visibility));
            expect_equal(
                slice(
                    text,
                    std::get<ASTPrivateDeclarationVisibility>(constant.visibility).keyword_span
                ),
                std::string_view("private")
            );
            expect_equal(slice(text, constant.name_span), std::string_view("answer"));
            if (!expect(constant.type.has_value())) {
                return;
            }
            expect(is<ASTNamedType>(ast.type(*constant.type)));
            expect_equal(
                slice(text, ast.expression(constant.initializer).span),
                std::string_view("42")
            );
            const auto& fragment = module_syntax.cpp_source_fragments.front();
            expect_equal(
                slice(text, fragment.payload_span),
                std::string_view("static_assert(true);\n")
            );
        };

    "Parser: C++ headers, fragments, and function forms retain distinct structure"_test =
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
            if (!expect(module_syntax.module_imports.empty())) {
                return;
            }
            if (!expect_equal(module_syntax.cpp_header_imports.size(), 2uz)) {
                return;
            }
            if (!expect_equal(module_syntax.cpp_source_fragments.size(), 1uz)) {
                return;
            }
            if (!expect_equal(module_syntax.items.size(), 3uz)) {
                return;
            }

            const auto& angle = module_syntax.cpp_header_imports[0];
            expect_equal(slice(text, angle.span), std::string_view("import <cstdint>;"));
            expect_equal(angle.delimiter, ASTCppHeaderDelimiter::AngleBrackets);
            expect_equal(slice(text, angle.name_span), std::string_view("cstdint"));
            const auto& quote = module_syntax.cpp_header_imports[1];
            expect_equal(
                slice(text, quote.span),
                std::string_view("import \"native/provider.hpp\";")
            );
            expect_equal(quote.delimiter, ASTCppHeaderDelimiter::Quotes);
            expect_equal(slice(text, quote.name_span), std::string_view("native/provider.hpp"));

            const auto& private_import = function(result, 0);
            expect(is<ASTPrivateDeclarationVisibility>(private_import.visibility));
            expect(is<ASTCppImportForm>(private_import.implementation));
            expect(!private_import.cpp_export.has_value());

            const auto& bare_import = function(result, 1);
            expect(is<ASTBareDeclarationVisibility>(bare_import.visibility));
            expect(is<ASTCppImportForm>(bare_import.implementation));

            const auto& cpp_export = function(result, 2);
            expect(is<ASTExportDeclarationVisibility>(cpp_export.visibility));
            if (!expect(cpp_export.cpp_export.has_value())) {
                return;
            }
            expect_equal(slice(text, cpp_export.cpp_export->span), std::string_view("export(cpp)"));
            expect(is<ASTFunctionBody>(cpp_export.implementation));

            const auto& fragment = module_syntax.cpp_source_fragments.front();
            expect_equal(
                slice(text, fragment.form_span),
                std::string_view("#[cpp] ---\nstatic_assert(true);\n---")
            );
            expect_equal(
                slice(text, fragment.payload_span),
                std::string_view("static_assert(true);\n")
            );
        };

    "Parser declaration: module and C++ header imports keep independent ownership"_test =
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
            if (!expect_equal(module_syntax.module_imports.size(), 2uz)) {
                return;
            }
            if (!expect_equal(module_syntax.cpp_header_imports.size(), 2uz)) {
                return;
            }

            expect_equal(
                slice(text, ast.module_import(module_syntax.module_imports[0]).span),
                std::string_view("import .first using First;")
            );
            expect_equal(
                slice(text, ast.module_import(module_syntax.module_imports[1]).span),
                std::string_view("import .second using Second;")
            );
            expect_equal(
                slice(text, module_syntax.cpp_header_imports[0].span),
                std::string_view("import <native/first.hpp>;")
            );
            expect_equal(
                slice(text, module_syntax.cpp_header_imports[1].span),
                std::string_view("import \"native/second.hpp\";")
            );
        };

    "Parser declaration: C++ source fragments remain independent module-owned spans"_test =
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
            if (!expect_equal(module_syntax.items.size(), 1uz)) {
                return;
            }
            if (!expect_equal(module_syntax.cpp_source_fragments.size(), 3uz)) {
                return;
            }
            expect_equal(
                slice(text, module_syntax.cpp_source_fragments[0].payload_span),
                std::string_view("first();\n")
            );
            expect_equal(
                slice(text, module_syntax.cpp_source_fragments[1].payload_span),
                std::string_view("auto raw = R\"(---)\";\n")
            );
            expect(module_syntax.cpp_source_fragments[2].payload_span.empty());

            check_invalid("#[cpp] ---\n---\n;");
        };

    "Parser declaration: enum cases retain positional payload type lists"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "enum Shape { Circle(f64), Rect(f64, f64,), Point, Tagged(Item) = 4, }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& enumeration = get<ASTEnumDecl>(item(result, 0));
            if (!expect_equal(enumeration.cases.size(), 4uz)) {
                return;
            }
            expect_equal(enumeration.cases[0].payload_types.size(), 1uz);
            expect_equal(enumeration.cases[1].payload_types.size(), 2uz);
            expect(enumeration.cases[2].payload_types.empty());
            if (!expect_equal(enumeration.cases[3].payload_types.size(), 1uz)) {
                return;
            }
            expect(is<ASTNamedType>(ast.type(enumeration.cases[3].payload_types[0])));
            expect(enumeration.cases[3].initializer.has_value());

            check_invalid("enum EmptyPayload { Case() }");
            check_invalid("enum MissingPayload { Case(i32, )");
        };

    "Parser declaration: imports form one contiguous nonempty-selection prefix"_test =
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
            each(invalid, std::identity {}, [](const auto& text) static noexcept {
                check_invalid(text);
            });
        };

    "Parser declaration: test declarations use distinct typed items"_test = [] static noexcept {
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
        if (!expect_equal(root(result).items.size(), 6uz)) {
            return;
        }
        expect(!(get<ASTTestDecl>(item(result, 0)).label.has_value()));
        expect(!(get<ASTTestDecl>(item(result, 1)).label.has_value()));
        const auto& empty = get<ASTTestDecl>(item(result, 2));
        if (!expect(empty.label.has_value())) {
            return;
        }
        expect_equal(slice(text, empty.label->span), std::string_view("\"\""));
        expect(empty.label->text.empty());
        const auto& declaration = get<ASTTestDecl>(item(result, 3));
        expect_equal(slice(text, declaration.keyword_span), std::string_view("test"));
        if (!expect(declaration.label.has_value())) {
            return;
        }
        expect_equal(slice(text, declaration.label->span), std::string_view("\"with body\""));
        expect_equal(declaration.label->text, std::string_view("with body"));
        expect_equal(ast.block(declaration.body).statements.size(), 1uz);
        expect(get<ASTTestDecl>(item(result, 4)).is_const);
        expect(!(get<ASTTestDecl>(item(result, 4)).label.has_value()));
        const auto& compile = get<ASTTestDecl>(item(result, 5));
        expect(compile.is_const);
        if (!expect(compile.label.has_value())) {
            return;
        }
        expect_equal(compile.label->text, std::string_view("compile"));

        check_invalid("test name {}", "expected '{'");
        check_invalid("test 42 {}", "expected '{'");
        check_invalid("test \"label\";", "expected '{'");
        check_invalid("const test name {}", "expected '{'");
        check_invalid(
            "export test \"name\" {}",
            "expected enum, struct, function, or const after visibility modifier"
        );
        check_invalid("fn nested() { test \"name\" {} }");
    };

    "Parser declaration: module references retain their distinct source forms"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "import model.user using User;\n"
                "import .sibling using Sibling;\n"
                "import json::parser.value using Value;\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& imports = root(result).module_imports;
            if (!expect_equal(imports.size(), 3uz)) {
                return;
            }

            const auto& domain_root = std::get<ASTDomainRootModuleReference>(
                ast.module_import(imports[0]).module_reference.value
            );
            if (!expect_equal(domain_root.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, domain_root.components.front()), std::string_view("model"));

            const auto& relative = std::get<ASTParentRelativeModuleReference>(
                ast.module_import(imports[1]).module_reference.value
            );
            expect_equal(slice(text, relative.prefix_span), std::string_view("."));
            if (!expect_equal(relative.components.size(), 1uz)) {
                return;
            }
            expect_equal(slice(text, relative.components.front()), std::string_view("sibling"));

            const auto& qualified = std::get<ASTCraftQualifiedModuleReference>(
                ast.module_import(imports[2]).module_reference.value
            );
            expect_equal(slice(text, qualified.name_span), std::string_view("json"));
            expect_equal(slice(text, qualified.separator_span), std::string_view("::"));
            if (!expect_equal(qualified.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, qualified.components.back()), std::string_view("value"));
        };

    "Parser: top-level constants require a name, initializer, and terminator"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "private const hidden = 1;\n"
                "const local: i32 = 2;\n"
                "export const shared: u32 = 3u32;\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            if (!expect_equal(root(result).items.size(), 3uz)) {
                return;
            }

            const auto& hidden = get<ASTConstantDecl>(item(result, 0));
            expect(std::holds_alternative<ASTPrivateDeclarationVisibility>(hidden.visibility));
            expect(!hidden.type.has_value());
            const auto& local = get<ASTConstantDecl>(item(result, 1));
            expect(std::holds_alternative<ASTBareDeclarationVisibility>(local.visibility));
            if (!expect(local.type.has_value())) {
                return;
            }
            const auto& shared = get<ASTConstantDecl>(item(result, 2));
            expect(std::holds_alternative<ASTExportDeclarationVisibility>(shared.visibility));
            expect_equal(
                slice(
                    text,
                    std::get<ASTExportDeclarationVisibility>(shared.visibility).keyword_span
                ),
                std::string_view("export")
            );
            if (!expect(shared.type.has_value())) {
                return;
            }
            expect_equal(
                slice(text, ast.expression(shared.initializer).span),
                std::string_view("3u32")
            );

            check_invalid("const _ = 1;", "a top-level constant requires a named target");
            check_invalid("const missing;");
            check_invalid("const missing =;");
            check_invalid("const missing = 1");
        };

    "Parser declaration: declaration diagnostics reject malformed forms"_test = [] static noexcept {
        const auto empty_structure = parse_valid("struct Empty {}");
        const auto& structure = get<ASTRecordDecl>(item(empty_structure, 0));
        expect(structure.fields.empty());
        const auto empty_enumeration = parse_valid("enum State {}");
        const auto& enumeration = get<ASTEnumDecl>(item(empty_enumeration, 0));
        expect(enumeration.cases.empty());

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
        each(invalid, std::identity {}, [](const auto& text) static noexcept {
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
    };

    "Parser declaration: exact underscore is a discard parameter target"_test = [] static noexcept {
        static constexpr auto text = std::string_view("fn discard(_: i32, &: i32) {}");
        check_invalid(text);

        static constexpr auto valid_text =
            std::string_view("fn discard(_: i32, &_ : i32, _name: i32) {}");
        const auto result = parse_valid(valid_text);
        const auto& parameters = function(result).parameters;
        if (!expect_equal(parameters.size(), 3uz)) {
            return;
        }
        expect(is<ASTDiscardBindingTarget>(parameters[0].target));
        expect_equal(parameters[0].access.mode, ASTAccessMode::Read);
        expect(!parameters[0].access.marker.has_value());
        expect(is<ASTDiscardBindingTarget>(parameters[1].target));
        expect_equal(parameters[1].access.mode, ASTAccessMode::Write);
        if (!expect(parameters[1].access.marker.has_value())) {
            return;
        }
        expect_equal(slice(valid_text, *parameters[1].access.marker), std::string_view("&"));
        expect(is<ASTNamedBindingTarget>(parameters[2].target));
        expect_equal(
            slice(valid_text, get<ASTNamedBindingTarget>(parameters[2].target).name_span),
            std::string_view("_name")
        );
    };

    "Parser declaration: callable parameters retain Read Write and Take access"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn access(view: i32, &update: i32, &&take: i32, "
                "callback: fn(i32, &i32, &&i32) -> i32) { "
                "let closure = [](value: i32, &changed: i32, &&owned: i32) {}; }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& parameters = function(result).parameters;

            if (!expect_equal(parameters.size(), 4uz)) {
                return;
            }
            expect_equal(parameters[0].access.mode, ASTAccessMode::Read);
            expect_equal(parameters[1].access.mode, ASTAccessMode::Write);
            expect_equal(parameters[2].access.mode, ASTAccessMode::Take);

            const auto& function_type = get<ASTFunctionType>(ast.type(*parameters[3].type));
            if (!expect_equal(function_type.parameters.size(), 3uz)) {
                return;
            }
            expect_equal(function_type.parameters[0].access.mode, ASTAccessMode::Read);
            expect_equal(function_type.parameters[1].access.mode, ASTAccessMode::Write);
            expect_equal(function_type.parameters[2].access.mode, ASTAccessMode::Take);

            const auto& body = function_body(result);
            const auto& binding = get<ASTVariableDecl>(ast.statement(body.statements[0]));
            const auto& closure = get<ASTLambdaExpr>(ast.expression(*binding.initializer));
            if (!expect_equal(closure.parameters.size(), 3uz)) {
                return;
            }
            expect_equal(closure.parameters[0].access.mode, ASTAccessMode::Read);
            expect_equal(closure.parameters[1].access.mode, ASTAccessMode::Write);
            expect_equal(closure.parameters[2].access.mode, ASTAccessMode::Take);

            check_invalid("fn invalid() { let closure = [&&value]() {}; }");
        };

    "Parser declaration: C++ selections retain qualified names and explicit namespaces"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "import <vector> using std::vector;\n"
                "import \"provider.hpp\" using vendor::{ Widget, create, };\n"
                "import <utility> using std::*;\n"
            );
            const auto tree = parse_valid(text);
            const auto& imports = root(tree).cpp_header_imports;
            if (!expect_equal(imports.size(), 3uz)) {
                return;
            }
            if (!expect(imports[0].using_clause.has_value())) {
                return;
            }
            const auto& first = *imports[0].using_clause;
            if (!expect_equal(first.prefix.size(), 1uz)) {
                return;
            }
            expect_equal(slice(text, first.prefix.front()), std::string_view("std"));
            expect_equal(
                slice(text, std::get<ASTCppSingleSelection>(first.selection).name),
                std::string_view("vector")
            );
            expect_equal(
                std::get<ASTCppListSelection>(imports[1].using_clause->selection).names.size(),
                2uz
            );
            expect(
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
            each(
                invalid,
                [](const char* source) static noexcept -> std::string_view { return source; },
                [](const char* source) static noexcept { check_invalid(source); }
            );
        };

    "Parser declaration: module components accept keyword spellings in every reference form"_test =
        [] static noexcept {
            const auto cases = std::to_array<std::string_view>({
                "import using using *;",
                "import .import.export using value;",
                "import match::using.true using {value};",
            });
            each(
                cases,
                [](std::string_view text) static noexcept -> std::string_view { return text; },
                [](std::string_view text) static noexcept {
                    const auto tree = parse_valid(text);
                    expect_equal(root(tree).module_imports.size(), 1uz);
                }
            );
        };

    "Parser declaration: callable expression bodies retain syntax and outer delimiters"_test =
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
            expect_equal(slice(source, body.arrow_span), std::string_view("=>"));
            expect(is<ASTBinaryExpr>(ast.expression(body.expression).value));
            const auto& outer =
                get<ASTExpressionBody>(get<ASTFunctionBody>(function(tree, 1).implementation).body);
            const auto& lambda = get<ASTLambdaExpr>(ast.expression(outer.expression).value);
            expect(is<ASTExpressionBody>(lambda.body));
            const auto invalid = std::to_array<std::string_view>({
                "fn missing() => ;",
                "fn missing() => 1",
                "import(cpp) fn invalid() => 1;",
                "fn invalid() { let f = []() => ; }",
                "fn invalid() => let x = 1;",
            });
            each(invalid, std::identity {}, [](const auto& text) static noexcept {
                check_invalid(text);
            });
        };

    "Parser declaration: top-level statements form one source-located implicit entry"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "let value = twice(21);\n"
                "fn twice(value: i32) => value * 2;\n"
                "const expected = 42;\n"
                "println(value);\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            if (!expect_equal(root(result).items.size(), 3uz)) {
                return;
            }
            expect(!(function(result, 0).is_implicit_entry));
            expect(is<ASTConstantDecl>(ast.item(root(result).items[1])));
            const auto& entry = function(result, 2);
            if (!expect(entry.is_implicit_entry)) {
                return;
            }
            expect(entry.parameters.empty());
            const auto& implementation = std::get<ASTFunctionBody>(entry.implementation);
            const auto& body = ast.block(std::get<ASTBlockID>(implementation.body));
            if (!expect_equal(body.statements.size(), 2uz)) {
                return;
            }
            expect_equal(
                slice(text, ast.statement(body.statements[0]).span),
                std::string_view("let value = twice(21);")
            );
            expect_equal(
                slice(text, ast.statement(body.statements[1]).span),
                std::string_view("println(value);")
            );
        };
    "Parser declaration: top-level static controls belong to the implicit entry"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "const answer = 2;\n"
                "const fn twice(value: i32) -> i32 => value * 2;\n"
                "const test { check(answer == 2); }\n"
                "const { assert(answer == 2); }\n"
                "const for value in [answer] { println(value); }\n"
                "const if true { println(answer); }\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            if (!expect_equal(root(result).items.size(), 5uz)) {
                return;
            }
            expect(is<ASTConstantDecl>(ast.item(root(result).items[0])));
            expect(function(result, 1).const_span.has_value());
            expect(is<ASTTestDecl>(ast.item(root(result).items[2])));
            expect(is<ASTConstBlock>(ast.item(root(result).items[3])));
            const auto& entry = function(result, 4);
            require(entry.is_implicit_entry);
            const auto& implementation = std::get<ASTFunctionBody>(entry.implementation);
            const auto& body = ast.block(std::get<ASTBlockID>(implementation.body));
            require(body.statements.size() == 2uz);
            expect(is<ASTForStmt>(ast.statement(body.statements[0])));
            expect(is<ASTIfForm>(ast.statement(body.statements[1])));
        };
    "Parser: named function parameters retain const qualifiers and spans"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn pick(value: i32, const self: usize) -> i32 => value;\n"
                "class Picker { fn get(self, const lane: usize) -> usize => lane; }\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& pick = function(result, 0);
            if (!expect_equal(pick.parameters.size(), 2u)) {
                return;
            }
            expect(!(pick.parameters[0].const_span.has_value()));
            const auto& static_parameter = pick.parameters[1];
            if (!expect(static_parameter.const_span.has_value())) {
                return;
            }
            expect_equal(slice(text, *static_parameter.const_span), "const");
            expect_equal(slice(text, static_parameter.span), "const self: usize");
            expect_equal(static_parameter.access.mode, ASTAccessMode::Read);

            const auto& record = get<ASTRecordDecl>(item(result, 1));
            if (!expect_equal(record.operations.size(), 1u)) {
                return;
            }
            const auto& method = get<ASTFunctionDecl>(ast.item(record.operations[0]));
            if (!expect_equal(method.parameters.size(), 2u)) {
                return;
            }
            if (!expect(method.parameters[1].const_span.has_value())) {
                return;
            }
            expect_equal(slice(text, *method.parameters[1].const_span), "const");
            expect_equal(slice(text, method.parameters[1].span), "const lane: usize");

            check_invalid("fn outer() { let callable = [](const index: usize) => index; }");
            check_invalid(
                "fn update(const &index: i32) {}",
                "const parameter cannot have an access marker"
            );
            check_invalid(
                "fn take(const &&index: i32) {}",
                "const parameter cannot have an access marker"
            );
        };
});

} // namespace
