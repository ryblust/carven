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
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Parser type: tuple elements retain nested product types"_test = [] static noexcept {
        const auto tree = parse_valid(
            "fn f(unit: (), grouped: (i32), single: (i32,), nested: (i32, (bool, ()),)) -> () {}"
        );
        const auto ast = tree.view();
        const auto& fn = function(tree);
        if (!expect_equal(fn.parameters.size(), 4uz)) {
            return;
        }
        expect(get<ASTTupleType>(ast.type(*fn.parameters[0].type)).elements.empty());
        expect(is<ASTNamedType>(ast.type(*fn.parameters[1].type)));
        expect_equal(get<ASTTupleType>(ast.type(*fn.parameters[2].type)).elements.size(), 1uz);
        const auto& outer = get<ASTTupleType>(ast.type(*fn.parameters[3].type));
        if (!expect_equal(outer.elements.size(), 2uz)) {
            return;
        }
        const auto& inner = get<ASTTupleType>(ast.type(outer.elements[1]));
        if (!expect_equal(inner.elements.size(), 2uz)) {
            return;
        }
        expect(get<ASTTupleType>(ast.type(inner.elements[1])).elements.empty());
        expect(get<ASTTupleType>(ast.type(*fn.result_type)).elements.empty());
        check_invalid("fn f(value: (,)) {}");
        check_invalid("fn f(value: (i32,,bool)) {}");
        check_invalid("fn f(value: (i32,bool) {} ");
    };

    "Parser type: recursive types retain grammar structure"_test = [] static noexcept {
        static constexpr auto text = std::string_view(
            "fn types(\n"
            " nested: [[u8; width]; height], callback: fn(i32, &State,) -> bool,\n"
            ") {}\n"
        );
        const auto result = parse_valid(text);
        const auto ast = result.view();
        const auto& fn = function(result);
        if (!expect_equal(fn.parameters.size(), 2uz)) {
            return;
        }

        const auto& nested = get<ASTArrayType>(ast.type(*fn.parameters[0].type));
        expect(is<ASTArrayType>(ast.type(nested.element_type)));
        expect(is<ASTNameExpr>(ast.expression(nested.extent)));

        const auto& callback = get<ASTFunctionType>(ast.type(*fn.parameters[1].type));
        if (!expect_equal(callback.parameters.size(), 2uz)) {
            return;
        }
        expect_equal(callback.parameters[0].access.mode, ASTAccessMode::Read);
        expect(!callback.parameters[0].access.marker.has_value());
        expect_equal(callback.parameters[1].access.mode, ASTAccessMode::Write);
        if (!expect(callback.parameters[1].access.marker.has_value())) {
            return;
        }
        expect_equal(slice(text, *callback.parameters[1].access.marker), std::string_view("&"));
        expect(is<ASTNamedType>(ast.type(callback.result_type)));
    };

    "Parser type: qualified components accept keywords while bare type names remain reserved"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn names(value: ::async::match, nested: vendor::await::var) -> ::test { "
                "return value as ::async::match; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& declaration = function(tree);
            const auto& global = get<ASTNamedType>(ast.type(*declaration.parameters[0].type));
            expect(global.global_root.has_value());
            if (!expect_equal(global.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, global.components[0].name_span), std::string_view("async"));
            expect_equal(slice(text, global.components[1].name_span), std::string_view("match"));
            const auto& nested = get<ASTNamedType>(ast.type(*declaration.parameters[1].type));
            expect(!nested.global_root.has_value());
            expect_equal(nested.components.size(), 3uz);
            const auto& result = get<ASTNamedType>(ast.type(*declaration.result_type));
            expect_equal(
                slice(text, result.components.front().name_span),
                std::string_view("test")
            );
            check_invalid("fn invalid(value: await) {}");
            check_invalid("fn invalid(value: ::vendor::7) {}");
        };

    "Parser type: global external type roots survive nested arguments and casts"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn f(value: ::std::vector<::std::vector<i32>>) -> ::Point {"
                " return value as ::Point; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& declaration = function(tree);
            const auto& outer = get<ASTNamedType>(ast.type(*declaration.parameters[0].type));
            if (!expect(outer.global_root.has_value())) {
                return;
            }
            expect_equal(slice(text, *outer.global_root), std::string_view("::"));
            if (!expect_equal(outer.arguments.size(), 1uz)) {
                return;
            }
            const auto& inner =
                get<ASTNamedType>(ast.type(std::get<ASTTypeID>(outer.arguments[0].value)));
            if (!expect(inner.global_root.has_value())) {
                return;
            }
            if (!expect_equal(inner.arguments.size(), 1uz)) {
                return;
            }
            const auto& builtin =
                get<ASTNamedType>(ast.type(std::get<ASTTypeID>(inner.arguments[0].value)));
            expect(!builtin.global_root.has_value());
        };

    "Parser type: failure arguments retain their effect role and source spans"_test =
        [] static noexcept {
            constexpr auto text =
                std::string_view("fn f(value: AsyncOperation<i32, throw Error + IO>) {}");
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& operation = get<ASTNamedType>(ast.type(*function(tree).parameters[0].type));
            if (!expect_equal(operation.arguments.size(), 2uz)) {
                return;
            }
            expect(std::holds_alternative<ASTTypeID>(operation.arguments[0].value));
            if (!expect(std::holds_alternative<ASTThrowClause>(operation.arguments[1].value))) {
                return;
            }
            const auto& failures = std::get<ASTThrowClause>(operation.arguments[1].value);
            expect_equal(failures.failures.size(), 2uz);
            expect_equal(
                slice(text, operation.arguments[1].span),
                std::string_view("throw Error + IO")
            );
            check_invalid("fn invalid(value: AsyncOperation<i32, throw>) {}");
        };

    "Parser type: explicit empty failure clauses retain their effect role"_test =
        [] static noexcept {
            constexpr auto text =
                std::string_view("fn f(value: AsyncOperation<i32, throw ()>) throw () {}");
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& declaration = function(tree);
            const auto& operation = get<ASTNamedType>(ast.type(*declaration.parameters[0].type));
            if (!expect_equal(operation.arguments.size(), 2uz)
                || !expect(std::holds_alternative<ASTThrowClause>(operation.arguments[1].value))
                || !expect(declaration.throw_clause.has_value())) {
                return;
            }
            expect(std::get<ASTThrowClause>(operation.arguments[1].value).failures.empty());
            expect(declaration.throw_clause->failures.empty());
            expect_equal(slice(text, operation.arguments[1].span), std::string_view("throw ()"));
            check_invalid("fn invalid() throw (Error) {}");
        };

    "Parser type: nested type argument clauses accept trailing commas"_test = [] static noexcept {
        const auto tree = parse_valid("fn use(value: Pair<Box<i32,>, [u8],>) {}");
        const auto ast = tree.view();
        const auto& declaration = function(tree);
        if (!expect_equal(declaration.parameters.size(), 1uz)
            || !expect(declaration.parameters[0].type.has_value())) {
            return;
        }
        const auto& pair = get<ASTNamedType>(ast.type(*declaration.parameters[0].type));
        if (!expect_equal(pair.arguments.size(), 2uz)) {
            return;
        }
        expect(is<ASTNamedType>(ast.type(std::get<ASTTypeID>(pair.arguments[0].value))));
        expect(is<ASTSliceType>(ast.type(std::get<ASTTypeID>(pair.arguments[1].value))));
        check_invalid("fn use(value: Pair<>) {}");
        check_invalid("fn use(value: Pair<i32,,u8>) {}");
    };
});

} // namespace
