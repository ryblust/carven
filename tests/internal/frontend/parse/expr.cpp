module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.expr;

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
import :source.text;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

auto body_of(const SyntaxTree& tree) noexcept -> const ASTBlock& {
    return function_body(tree);
}

auto expression_statement(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTExpr& {
    const auto ast = tree.view();
    const auto& statement = ast.statement(body_of(tree).statements[index]);
    return ast.expression(get<ASTExprStatement>(statement).expression);
}

auto initializer(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTExpr& {
    const auto ast = tree.view();
    const auto& declaration = get<ASTVariableDecl>(ast.statement(body_of(tree).statements[index]));
    return ast.expression(*declaration.initializer);
}

} // namespace

TEST_CASE("Parser: literals retain typed lexer values") {
    static constexpr auto text = std::string_view(
        "fn values() { 1; 1f32; 1.0; 0xffu8; 0b10; 0o7; 'x'; \"text\"; true; false; }"
    );
    const auto result = parse_valid(text);
    const auto literal_at = [&](std::size_t index) noexcept -> const ASTLiteral& {
        return get<ASTLiteral>(expression_statement(result, index));
    };
    CHECK_EQ(std::get<IntegerLiteralValue>(literal_at(0).value).base, IntegerBase::Decimal);
    CHECK_EQ(std::get<FloatingLiteralValue>(literal_at(1).value).suffix, NumericSuffix::F32);
    CHECK_EQ(slice(text, std::get<FloatingLiteralValue>(literal_at(1).value).value_span), "1");
    CHECK_EQ(std::get<IntegerLiteralValue>(literal_at(3).value).base, IntegerBase::Hexadecimal);
    CHECK_EQ(std::get<IntegerLiteralValue>(literal_at(4).value).base, IntegerBase::Binary);
    CHECK_EQ(std::get<IntegerLiteralValue>(literal_at(5).value).base, IntegerBase::Octal);
    CHECK_EQ(std::get<CharacterLiteralValue>(literal_at(6).value).scalar, U'x');
    CHECK_EQ(std::get<StringLiteralValue>(literal_at(7).value).bytes, "text");
    CHECK(std::get<BooleanLiteralValue>(literal_at(8).value).value);
    CHECK(!std::get<BooleanLiteralValue>(literal_at(9).value).value);
}

TEST_CASE("Parser: primary and postfix alternatives remain structural") {
    static constexpr auto text = std::string_view(
        "fn expressions() {"
        " let empty = []; let grouped = (value); let defaulted = Box {};"
        " let positional = Pair { 1, 2, }; let fields = Point { x: 1, y: 2, };"
        " factory(1, 2,)[index].field::member;"
        " let callback = fn(i32) -> bool { predicate };"
        "}"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    CHECK(get<ASTArrayExpr>(initializer(result, 0)).element_ids.empty());
    CHECK(is<ASTGroupExpr>(initializer(result, 1)));

    const auto& defaulted = get<ASTConstructionExpr>(initializer(result, 2));
    CHECK(is<ASTNamedType>(defaulted.type));
    CHECK(is<std::monostate>(defaulted.initializer));

    const auto& positional = get<ASTConstructionExpr>(initializer(result, 3));
    CHECK_EQ(get<ASTPositionalInitializerList>(positional.initializer).values.size(), 2u);
    const auto& fields = get<ASTConstructionExpr>(initializer(result, 4));
    CHECK_EQ(get<ASTFieldInitializerList>(fields.initializer).fields.size(), 2u);

    const auto& final_member = get<ASTMemberExpr>(expression_statement(result, 5));
    CHECK_EQ(slice(text, final_member.name_span), "member");
    const auto& field = get<ASTMemberExpr>(ast.expression(final_member.operand_id));
    const auto& index = get<ASTIndexExpr>(ast.expression(field.operand_id));
    const auto& call = get<ASTCallExpr>(ast.expression(index.operand_id));
    CHECK_EQ(call.arguments.size(), 2u);

    CHECK(is<ASTFunctionType>(get<ASTConstructionExpr>(initializer(result, 6)).type));

    check_invalid("fn invalid() { let native = #[cpp] ---\nreturn compute();\n---\n; }");
}

TEST_CASE("Parser: precedence is represented by typed expression edges") {
    const auto result = parse_valid(
        "fn expression() { result = !-value + call(1, 2,)[index].field * 3 "
        "<< 1 == limit && ready || done; }"
    );
    const auto ast = result.view();
    const auto& assignment = get<ASTAssignment>(ast.statement(body_of(result).statements[0]));
    const auto& logical_or = get<ASTBinaryExpr>(ast.expression(assignment.value));
    CHECK_EQ(logical_or.op, ASTBinaryOperator::LogicalOr);
    CHECK(is<ASTNameExpr>(ast.expression(logical_or.right)));
    const auto& logical_and = get<ASTBinaryExpr>(ast.expression(logical_or.left));
    CHECK_EQ(logical_and.op, ASTBinaryOperator::LogicalAnd);
    const auto& comparison = get<ASTBinaryExpr>(ast.expression(logical_and.left));
    CHECK_EQ(comparison.op, ASTBinaryOperator::Equal);

    check_invalid("fn expression() { a < b < c; }");
    check_invalid("fn expression() { (a, b); }");
}

TEST_CASE("Parser: as is left associative between prefix and multiplicative expressions") {
    const auto result = parse_valid(
        "fn expression() { let value = -input as i32 as i64 * 2 + 1; update(&input); }"
    );
    const auto ast = result.view();
    const auto& additive = get<ASTBinaryExpr>(initializer(result, 0));
    const auto& multiply = get<ASTBinaryExpr>(ast.expression(additive.left));
    const auto& outer_cast = get<ASTCastExpr>(ast.expression(multiply.left));
    const auto& inner_cast = get<ASTCastExpr>(ast.expression(outer_cast.operand_id));
    CHECK(is<ASTPrefixExpr>(ast.expression(inner_cast.operand_id)));

    const auto& call = get<ASTCallExpr>(expression_statement(result, 1));
    REQUIRE_EQ(call.arguments.size(), 1u);
    const auto& access = get<ASTAccessExpr>(ast.expression(call.arguments.front().expression));
    CHECK_EQ(access.mode, ASTAccessMode::Write);
    CHECK(is<ASTNameExpr>(ast.expression(access.operand_id)));
}

TEST_CASE("Parser: access expressions cover the following complete expression") {
    const auto result = parse_valid(
        "fn access() { let taken = &&left + right; let updated = &(target); "
        "let logical = left && right; let bitwise = left & right; }"
    );
    const auto ast = result.view();

    const auto& take = get<ASTAccessExpr>(initializer(result, 0));
    CHECK_EQ(take.mode, ASTAccessMode::Take);
    CHECK(is<ASTBinaryExpr>(ast.expression(take.operand_id)));

    const auto& write = get<ASTAccessExpr>(initializer(result, 1));
    CHECK_EQ(write.mode, ASTAccessMode::Write);
    CHECK(is<ASTGroupExpr>(ast.expression(write.operand_id)));

    CHECK_EQ(get<ASTBinaryExpr>(initializer(result, 2)).op, ASTBinaryOperator::LogicalAnd);
    CHECK_EQ(get<ASTBinaryExpr>(initializer(result, 3)).op, ASTBinaryOperator::BitwiseAnd);
}

TEST_CASE("Parser: prefix expressions preserve written nesting") {
    const auto result = parse_valid("fn prefix() { !-~value; }");
    const auto ast = result.view();
    const auto& logical_not = get<ASTPrefixExpr>(expression_statement(result, 0));
    CHECK_EQ(logical_not.op, ASTPrefixOperator::LogicalNot);
    const auto& negate = get<ASTPrefixExpr>(ast.expression(logical_not.operand_id));
    CHECK_EQ(negate.op, ASTPrefixOperator::Negate);
    const auto& complement = get<ASTPrefixExpr>(ast.expression(negate.operand_id));
    CHECK_EQ(complement.op, ASTPrefixOperator::BitwiseNot);
    CHECK(is<ASTNameExpr>(ast.expression(complement.operand_id)));
}

TEST_CASE("Parser: contextual and qualified enum cases are structural expressions") {
    static constexpr auto text = std::string_view(
        "fn cases() { let point = .Point; let circle = .Circle(1.0); Shape::Circle; }"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();

    const auto& point = get<ASTContextualCaseExpr>(initializer(result, 0));
    CHECK_EQ(slice(text, point.dot_span), ".");
    CHECK_EQ(slice(text, point.name_span), "Point");

    const auto& call = get<ASTCallExpr>(initializer(result, 1));
    CHECK(is<ASTContextualCaseExpr>(ast.expression(call.callee)));
    REQUIRE_EQ(call.arguments.size(), 1u);

    const auto& qualified = get<ASTMemberExpr>(expression_statement(result, 2));
    CHECK_EQ(qualified.op, ASTMemberOperator::Scope);
    CHECK_EQ(slice(text, qualified.name_span), "Circle");
    CHECK(is<ASTNameExpr>(ast.expression(qualified.operand_id)));

    check_invalid("fn f() { let value = .; }");
}
