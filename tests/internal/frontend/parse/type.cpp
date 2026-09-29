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

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Parser type: recursive types retain grammar structure", [] static noexcept {
        static constexpr auto text = std::string_view(
            "fn types(\n"
            " nested: [[u8; width]; height], callback: fn(i32, &State,) -> bool,\n"
            ") {}\n"
        );
        const auto result = parse_valid(text);
        const auto ast = result.view();
        const auto& fn = function(result);
        if (!ct::expect_equal(fn.parameters.size(), 2uz)) {
            return;
        }

        const auto& nested = get<ASTArrayType>(ast.type(*fn.parameters[0].type));
        ct::expect(is<ASTArrayType>(ast.type(nested.element_type)));
        ct::expect(is<ASTNameExpr>(ast.expression(nested.extent)));

        const auto& callback = get<ASTFunctionType>(ast.type(*fn.parameters[1].type));
        if (!ct::expect_equal(callback.parameters.size(), 2uz)) {
            return;
        }
        ct::expect_equal(callback.parameters[0].access.mode, ASTAccessMode::Read);
        ct::expect(!callback.parameters[0].access.marker.has_value());
        ct::expect_equal(callback.parameters[1].access.mode, ASTAccessMode::Write);
        if (!ct::expect(callback.parameters[1].access.marker.has_value())) {
            return;
        }
        ct::expect_equal(slice(text, *callback.parameters[1].access.marker), std::string_view("&"));
        ct::expect(is<ASTNamedType>(ast.type(callback.result_type)));
    });

    ct::test(
        "Parser type: global external type roots survive nested arguments and casts",
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn f(value: ::std::vector<::std::vector<i32>>) -> ::Point {"
                " return value as ::Point; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& declaration = function(tree);
            const auto& outer = get<ASTNamedType>(ast.type(*declaration.parameters[0].type));
            if (!ct::expect(outer.global_root.has_value())) {
                return;
            }
            ct::expect_equal(slice(text, *outer.global_root), std::string_view("::"));
            if (!ct::expect_equal(outer.arguments.size(), 1uz)) {
                return;
            }
            const auto& inner = get<ASTNamedType>(ast.type(outer.arguments[0]));
            if (!ct::expect(inner.global_root.has_value())) {
                return;
            }
            if (!ct::expect_equal(inner.arguments.size(), 1uz)) {
                return;
            }
            const auto& builtin = get<ASTNamedType>(ast.type(inner.arguments[0]));
            ct::expect(!builtin.global_root.has_value());
        }
    );
});

} // namespace
