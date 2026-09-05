module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

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
import :test.internal.frontend.parse.fixture;
import std;

TEST_CASE("Parser: ordinary and branch blocks preserve different result rules") {
    const auto result = parse_valid(
        "fn choose(ready) {\n"
        " if ready { work(); } else { fallback(); }\n"
        " let value = if ready { work(); 1 } else { 0 };\n"
        "}\n"
    );
    const auto ast = result.view();
    const auto& body = function_body(result);
    REQUIRE_EQ(body.statements.size(), 2u);

    const auto& statement_if = get<ASTIfForm>(ast.statement(body.statements[0]));
    CHECK(!ast.branch_block(statement_if.branches[0].body).result.has_value());
    REQUIRE(statement_if.else_branch.has_value());
    CHECK(!ast.branch_block(*statement_if.else_branch).result.has_value());

    const auto& declaration = get<ASTVariableDecl>(ast.statement(body.statements[1]));
    const auto& value_if = get<ASTIfForm>(ast.expression(*declaration.initializer));
    CHECK(ast.branch_block(value_if.branches[0].body).result.has_value());
    REQUIRE(value_if.else_branch.has_value());
    CHECK(ast.branch_block(*value_if.else_branch).result.has_value());
}

TEST_CASE("Parser: patterns own inline type children and typed match-arm bodies") {
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
    REQUIRE_EQ(match.arms.size(), 4u);

    CHECK(is<ASTWildcardPattern>(ast.pattern(match.arms[0].pattern)));
    CHECK(is<ASTExprID>(match.arms[0].body));

    const auto& alternatives = get<ASTOrPattern>(ast.pattern(match.arms[1].pattern));
    REQUIRE_EQ(alternatives.alternatives.size(), 3u);
    const auto& negative = get<ASTNegativeNumberPattern>(ast.pattern(alternatives.alternatives[0]));
    CHECK_EQ(std::get<IntegerLiteralValue>(negative.value).magnitude, 1u);
    CHECK(is<ASTLiteral>(ast.pattern(alternatives.alternatives[1])));
    CHECK(is<ASTCasePattern>(ast.pattern(alternatives.alternatives[2])));
    CHECK(match.arms[1].guard.has_value());
    CHECK(is<ASTNameExpr>(ast.expression(match.arms[1].guard->expression)));
    CHECK(is<ASTBranchBlockID>(match.arms[1].body));

    const auto& number_constraint = get<ASTConstraintPattern>(ast.pattern(match.arms[2].pattern));
    CHECK(is<ASTQualifiedName>(number_constraint.operand));
    CHECK(is<ASTControlTransfer>(match.arms[2].body));

    const auto& array_constraint = get<ASTConstraintPattern>(ast.pattern(match.arms[3].pattern));
    CHECK(is<ASTArrayType>(array_constraint.operand));
    REQUIRE_EQ(ast.types().size(), 1u);
    CHECK(is<ASTNamedType>(ast.types().front()));

    parse_valid("fn f() { match value { _ | 1 => value() } }");
    check_invalid("fn f() { match value { is 1 => value() } }");
    check_invalid("fn f() { match value { 0 => return value; } }");
    check_invalid(
        "fn f() { match value { -State => value } }",
        "expected number after '-' in pattern"
    );
}

TEST_CASE("Parser: enum case patterns recursively own positional patterns") {
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
    REQUIRE_EQ(match.arms.size(), 3u);

    const auto& circle = get<ASTCasePattern>(ast.pattern(match.arms[0].pattern));
    CHECK(std::holds_alternative<ASTContextualCaseQualifier>(circle.qualifier));
    REQUIRE(circle.payload.has_value());
    REQUIRE_EQ(circle.payload->patterns.size(), 1u);
    CHECK(is<ASTBindingPattern>(ast.pattern(circle.payload->patterns[0])));
    REQUIRE(match.arms[0].guard.has_value());
    CHECK(is<ASTBinaryExpr>(ast.expression(match.arms[0].guard->expression)));

    const auto& rectangle = get<ASTCasePattern>(ast.pattern(match.arms[1].pattern));
    REQUIRE(std::holds_alternative<ASTQualifiedCaseQualifier>(rectangle.qualifier));
    CHECK_EQ(std::get<ASTQualifiedCaseQualifier>(rectangle.qualifier).components.size(), 1u);
    REQUIRE(rectangle.payload.has_value());
    REQUIRE_EQ(rectangle.payload->patterns.size(), 2u);
    CHECK(is<ASTWildcardPattern>(ast.pattern(rectangle.payload->patterns[0])));
    const auto& nested_or = get<ASTOrPattern>(ast.pattern(rectangle.payload->patterns[1]));
    REQUIRE_EQ(nested_or.alternatives.size(), 2u);
    CHECK(is<ASTCasePattern>(ast.pattern(nested_or.alternatives[0])));
    CHECK(is<ASTCasePattern>(ast.pattern(nested_or.alternatives[1])));

    const auto& point = get<ASTCasePattern>(ast.pattern(match.arms[2].pattern));
    CHECK(!point.payload.has_value());

    check_invalid("fn f(x) { match x { .Case(value => value } }");
}

TEST_CASE("Parser: semantic invalidity does not reject a grammar AST") {
    static constexpr auto accepted = std::to_array<std::string_view>({
        "fn main(args: i32, extra) -> Text { break; }",
        "fn f() { return; continue; }",
        "fn f() { let value = if ready { 1 }; }",
        "fn f() { let value = match subject {}; }",
        "fn f() { match subject { _ => first(), 0 => unreachable() } }",
    });
    for (const auto& text : accepted) {
        parse_valid(text);
    }
}
