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
            const auto& inner = get<ASTNamedType>(ast.type(outer.arguments[0]));
            if (!expect(inner.global_root.has_value())) {
                return;
            }
            if (!expect_equal(inner.arguments.size(), 1uz)) {
                return;
            }
            const auto& builtin = get<ASTNamedType>(ast.type(inner.arguments[0]));
            expect(!builtin.global_root.has_value());
        };
});

} // namespace
