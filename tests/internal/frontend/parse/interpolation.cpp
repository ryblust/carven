module carven:test.internal.frontend.parse.interpolation;

import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Parser interpolation: interpolation preserves expressions and specification spans"_test =
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
            if (!expect(interpolation.parts.size() == 2uz)) {
                return;
            }
            const auto* hole = std::get_if<ASTInterpolationHole>(&interpolation.parts[1].value);
            if (!expect(hole != nullptr)) {
                return;
            }
            if (!expect(hole->colon_span.has_value())) {
                return;
            }
            expect(slice(source, *hole->colon_span) == ":");
            expect(slice(source, ast.expression(hole->expression).span) == "x");
            if (!expect(hole->specification.size() == 2uz)) {
                return;
            }
            expect(slice(source, hole->specification[0].span) == "{width}");
            expect(slice(source, hole->specification[1].span) == ".2f");
        };

    "Parser: interpolation requires expressions and ordinary literal positions stay literal"_test =
        [] static noexcept {
            const auto sources = std::array {
                R"(fn bad() { let s = f"{}"; })",
                R"(fn bad() { let s = f"{:04x}"; })",
                R"(fn bad() { let s = f"{1 +}"; })",
                R"(fn bad() { let s = f"{1:{}}"; })",
                R"(test f"name" {})",
                R"(fn bad(s: str) { match s { f"x" => {}, } })",
            };
            each(sources, std::identity {}, [](const char* source) static noexcept {
                check_rejected(source);
            });
        };
});

} // namespace
