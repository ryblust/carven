module carven:test.internal.frontend.parse.interpolation;

import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Parser interpolation: interpolation preserves expressions and specification spans",
        [] static noexcept {
            const auto source = std::string_view(
                R"(fn value(x: f64, width: i32) { let s = f"value {x:{width}.2f}"; })"
            );
            const auto tree = parse_valid(source);
            const auto ast = tree.view();
            // The body initializer is an ordinary expression whose parts retain their source locations.
            const auto& statement = ast.statement(function_body(tree).statements.front());
            const auto& binding = get<ASTVariableDecl>(statement);
            const auto& interpolation =
                get<ASTInterpolationExpr>(ast.expression(*binding.initializer));
            if (!ct::expect(interpolation.parts.size() == 2uz)) {
                return;
            }
            const auto* hole = std::get_if<ASTInterpolationHole>(&interpolation.parts[1].value);
            if (!ct::expect(hole != nullptr)) {
                return;
            }
            if (!ct::expect(hole->colon_span.has_value())) {
                return;
            }
            ct::expect(slice(source, *hole->colon_span) == ":");
            ct::expect(slice(source, ast.expression(hole->expression).span) == "x");
            if (!ct::expect(hole->specification.size() == 2uz)) {
                return;
            }
            ct::expect(slice(source, hole->specification[0].span) == "{width}");
            ct::expect(slice(source, hole->specification[1].span) == ".2f");
        }
    );

    ct::test(
        "Parser: interpolation requires expressions and ordinary literal positions stay literal",
        [] static noexcept {
            const auto sources = std::array {
                R"(fn bad() { let s = f"{}"; })",
                R"(fn bad() { let s = f"{:04x}"; })",
                R"(fn bad() { let s = f"{1 +}"; })",
                R"(fn bad() { let s = f"{1:{}}"; })",
                R"(test f"name" {})",
                R"(fn bad(s: str) { match s { f"x" => {}, } })",
            };
            ct::each(sources, std::identity {}, [](const char* source) static noexcept {
                check_rejected(source);
            });
        }
    );
});

} // namespace
