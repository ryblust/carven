module carven:test.internal.frontend.parse.control;

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
import :frontend.literal;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Parser control: ordinary and branch blocks preserve different result rules",
        [] static noexcept {
            const auto result = parse_valid(
                "fn choose(ready) {\n"
                " if ready { work(); } else { fallback(); }\n"
                " let value = if ready { work(); 1 } else { 0 };\n"
                "}\n"
            );
            const auto ast = result.view();
            const auto& body = function_body(result);
            if (!ct::expect_equal(body.statements.size(), 2uz)) {
                return;
            }

            const auto& statement_if = get<ASTIfForm>(ast.statement(body.statements[0]));
            ct::expect(!ast.branch_block(statement_if.branches[0].body).result.has_value());
            if (!ct::expect(statement_if.else_branch.has_value())) {
                return;
            }
            ct::expect(!ast.branch_block(*statement_if.else_branch).result.has_value());

            const auto& declaration = get<ASTVariableDecl>(ast.statement(body.statements[1]));
            const auto& value_if = get<ASTIfForm>(ast.expression(*declaration.initializer));
            ct::expect(ast.branch_block(value_if.branches[0].body).result.has_value());
            if (!ct::expect(value_if.else_branch.has_value())) {
                return;
            }
            ct::expect(ast.branch_block(*value_if.else_branch).result.has_value());
        }
    );

    ct::test(
        "Parser control: patterns own inline type children and typed match-arm bodies",
        [] static noexcept {
            const auto result = parse_valid(
                "fn classify(value) {\n"
                " match value {\n"
                "  _ => fallback(),\n"
                "  -1 | 0 | State::Ready if ready => { prepare(); result },\n"
                "  is Number => return 1,\n"
                "  is [i32; 4] => continue,\n"
                " }\n"
                "}\n"
            );
            const auto ast = result.view();
            const auto& body = function_body(result);
            const auto& match = get<ASTMatchForm>(ast.statement(body.statements[0]));
            if (!ct::expect_equal(match.arms.size(), 4uz)) {
                return;
            }

            ct::expect(is<ASTWildcardPattern>(ast.pattern(match.arms[0].pattern)));
            ct::expect(is<ASTExprID>(match.arms[0].body));

            const auto& alternatives = get<ASTOrPattern>(ast.pattern(match.arms[1].pattern));
            if (!ct::expect_equal(alternatives.alternatives.size(), 3uz)) {
                return;
            }
            const auto& negative =
                get<ASTNegativeNumberPattern>(ast.pattern(alternatives.alternatives[0]));
            ct::expect_equal(std::get<IntegerLiteralValue>(negative.value).magnitude, 1u);
            ct::expect(is<ASTLiteral>(ast.pattern(alternatives.alternatives[1])));
            ct::expect(is<ASTCasePattern>(ast.pattern(alternatives.alternatives[2])));
            ct::expect(match.arms[1].guard.has_value());
            ct::expect(is<ASTNameExpr>(ast.expression(match.arms[1].guard->expression)));
            ct::expect(is<ASTBranchBlockID>(match.arms[1].body));

            const auto& number_constraint =
                get<ASTConstraintPattern>(ast.pattern(match.arms[2].pattern));
            ct::expect(is<ASTQualifiedName>(number_constraint.operand));
            ct::expect(is<ASTControlTransfer>(match.arms[2].body));

            const auto& array_constraint =
                get<ASTConstraintPattern>(ast.pattern(match.arms[3].pattern));
            ct::expect(is<ASTArrayType>(array_constraint.operand));
            if (!ct::expect_equal(ast.types().size(), 1uz)) {
                return;
            }
            ct::expect(is<ASTNamedType>(ast.types().front()));

            parse_valid("fn f() { match value { _ | 1 => value() } }");
            check_invalid("fn f() { match value { is 1 => value() } }");
            check_invalid("fn f() { match value { 0 => return value; } }");
            check_invalid(
                "fn f() { match value { -State => value } }",
                "expected number after '-' in pattern"
            );
        }
    );

    ct::test(
        "Parser control: enum case patterns recursively own positional patterns",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn inspect(shape) { match shape {"
                " .Circle(radius) if radius > 0.0 => radius,"
                " Shape::Rect(_, .Some(value) | .None) => value,"
                " .Point => 0.0,"
                " } }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& body = function_body(result);
            const auto& match = get<ASTMatchForm>(ast.statement(body.statements[0]));
            if (!ct::expect_equal(match.arms.size(), 3uz)) {
                return;
            }

            const auto& circle = get<ASTCasePattern>(ast.pattern(match.arms[0].pattern));
            ct::expect(std::holds_alternative<ASTContextualCaseQualifier>(circle.qualifier));
            if (!ct::expect(circle.payload.has_value())) {
                return;
            }
            if (!ct::expect_equal(circle.payload->patterns.size(), 1uz)) {
                return;
            }
            ct::expect(is<ASTBindingPattern>(ast.pattern(circle.payload->patterns[0])));
            if (!ct::expect(match.arms[0].guard.has_value())) {
                return;
            }
            ct::expect(is<ASTBinaryExpr>(ast.expression(match.arms[0].guard->expression)));

            const auto& rectangle = get<ASTCasePattern>(ast.pattern(match.arms[1].pattern));
            if (!ct::expect(
                    std::holds_alternative<ASTQualifiedCaseQualifier>(rectangle.qualifier)
                )) {
                return;
            }
            ct::expect_equal(
                std::get<ASTQualifiedCaseQualifier>(rectangle.qualifier).components.size(),
                1uz
            );
            if (!ct::expect(rectangle.payload.has_value())) {
                return;
            }
            if (!ct::expect_equal(rectangle.payload->patterns.size(), 2uz)) {
                return;
            }
            ct::expect(is<ASTWildcardPattern>(ast.pattern(rectangle.payload->patterns[0])));
            const auto& nested_or = get<ASTOrPattern>(ast.pattern(rectangle.payload->patterns[1]));
            if (!ct::expect_equal(nested_or.alternatives.size(), 2uz)) {
                return;
            }
            ct::expect(is<ASTCasePattern>(ast.pattern(nested_or.alternatives[0])));
            ct::expect(is<ASTCasePattern>(ast.pattern(nested_or.alternatives[1])));

            const auto& point = get<ASTCasePattern>(ast.pattern(match.arms[2].pattern));
            ct::expect(!point.payload.has_value());

            check_invalid("fn f(x) { match x { .Case(value => value } }");
        }
    );

    ct::test(
        "Parser control: semantic invalidity does not reject a grammar AST",
        [] static noexcept {
            static constexpr auto accepted = std::to_array<std::string_view>({
                "fn main(args: i32, extra) -> Text { break; }",
                "fn f() { return; continue; }",
                "fn f() { let value = if ready { 1 }; }",
                "fn f() { let value = match subject {}; }",
                "fn f() { match subject { _ => first(), 0 => unreachable() } }",
            });
            ct::each(accepted, std::identity {}, [](std::string_view text) static noexcept {
                parse_valid(text);
            });
        }
    );

    ct::test(
        "Parser control: outer parentheses do not change nested block boundaries",
        [] static noexcept {
            auto expression = std::string("1");
            for (auto depth = 0uz; depth < 60uz; ++depth) {
                expression.insert(0, "if ready { (");
                expression += ") } else { 2 }";
            }
            const auto result = parse_valid("fn nested(ready) => (" + expression + ");");
            ct::expect_equal(root(result).items.size(), 1uz);
            check_invalid("fn nested(ready) => (if ready { (if ready { 1 } else 2) } else { 3 });");
            static_cast<void>(
                parse_valid("fn nested() => (if (Flag { true }).value { 1 } else { 2 });")
            );
        }
    );
});

} // namespace
