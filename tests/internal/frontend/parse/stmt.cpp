module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.parse.stmt;

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
import :test.internal.frontend.parse.fixture;
import std;

namespace {
auto body_of(const SyntaxTree& tree) noexcept -> const ASTBlock& {
    return function_body(tree);
}

auto statement_at(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTStmt& {
    return tree.view().statement(body_of(tree).statements[index]);
}
} // namespace

TEST_CASE("Parser: else-if chains use an ordered branch list") {
    static constexpr auto text = std::string_view(
        "fn classify(value: i32) {\n"
        " if value == 0 { return; }\n"
        " else if value == 1 { return; }\n"
        " else if value == 2 { return; }\n"
        " else { return; }\n"
        "}\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& conditional = get<ASTIfForm>(statement_at(result, 0));
    REQUIRE_EQ(conditional.branches.size(), 3u);
    REQUIRE(conditional.else_branch.has_value());
    CHECK_EQ(slice(text, ast.expression(conditional.branches[0].condition).span), "value == 0");
    CHECK_EQ(slice(text, ast.expression(conditional.branches[1].condition).span), "value == 1");
    CHECK_EQ(slice(text, ast.expression(conditional.branches[2].condition).span), "value == 2");
}

TEST_CASE("Parser: declarations, actions, and loop headers remain distinct") {
    static constexpr auto text = std::string_view(
        "fn actions(values) {\n"
        " let first: i32 = 1; target += first; ++target;\n"
        " while ready { tick(); }\n"
        " for &item: Item in values { mutate(item); }\n"
        " for var i: i32 = 0; i < 10; i += 1, ++count, tick() { consume(i); }\n"
        "}\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    REQUIRE_EQ(body_of(result).statements.size(), 6u);

    const auto& declaration = get<ASTVariableDecl>(statement_at(result, 0));
    CHECK_EQ(declaration.kind, ASTBindingKind::Let);
    CHECK(declaration.type.has_value());

    const auto& assignment = get<ASTAssignment>(statement_at(result, 1));
    CHECK_EQ(assignment.op, ASTAssignmentOperator::Add);
    CHECK(is<ASTNameExpr>(ast.expression(assignment.target)));

    const auto& update = get<ASTUpdate>(statement_at(result, 2));
    CHECK_EQ(update.op, ASTUpdateOperator::Increment);
    CHECK(is<ASTWhileStmt>(statement_at(result, 3)));

    const auto& range_loop = get<ASTForStmt>(statement_at(result, 4));
    const auto& range = get<ASTRangeForHeader>(range_loop.header);
    REQUIRE(range.write_marker.has_value());
    CHECK_EQ(slice(text, *range.write_marker), "&");
    CHECK(range.type.has_value());
    REQUIRE(std::holds_alternative<ASTExprID>(range.iterable));
    CHECK(is<ASTNameExpr>(ast.expression(std::get<ASTExprID>(range.iterable))));

    const auto& c_style_loop = get<ASTForStmt>(statement_at(result, 5));
    const auto& c_style = get<ASTCStyleForHeader>(c_style_loop.header);
    CHECK(is<ASTVariableDecl>(c_style.initializer));
    CHECK(c_style.condition.has_value());
    REQUIRE_EQ(c_style.steps.size(), 3u);
    CHECK(is<ASTAssignment>(c_style.steps[0]));
    CHECK(is<ASTUpdate>(c_style.steps[1]));
    CHECK(is<ASTExprID>(c_style.steps[2]));
}

TEST_CASE("Parser: range-for sources distinguish containers and half-open bounds") {
    static constexpr auto text = std::string_view(
        "fn ranges(items, begin, end) {\n"
        " for item in items {} for i in 1..10 {}\n"
        " for &i in begin..end() {} for i in (begin + 1)..(end - 1) {}\n"
        "}\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();

    const auto header_at = [&](std::size_t index) noexcept -> const ASTRangeForHeader& {
        return get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, index)).header);
    };
    const auto& container = header_at(0);
    REQUIRE(is<ASTExprID>(container.iterable));
    const auto container_id = std::get<ASTExprID>(container.iterable);
    CHECK(is<ASTNameExpr>(ast.expression(container_id)));
    CHECK_EQ(slice(text, ast.expression(container_id).span), "items");

    const auto& literal = std::get<ASTHalfOpenRange>(header_at(1).iterable);
    CHECK(is<ASTLiteral>(ast.expression(literal.begin)));
    CHECK(is<ASTLiteral>(ast.expression(literal.end)));
    CHECK_EQ(slice(text, literal.operator_span), "..");

    const auto& variable = std::get<ASTHalfOpenRange>(header_at(2).iterable);
    CHECK(is<ASTNameExpr>(ast.expression(variable.begin)));

    check_invalid("fn invalid(values) { for &&value in values {} }");
    CHECK(is<ASTCallExpr>(ast.expression(variable.end)));

    const auto& grouped = std::get<ASTHalfOpenRange>(header_at(3).iterable);
    CHECK(is<ASTGroupExpr>(ast.expression(grouped.begin)));
    CHECK(is<ASTGroupExpr>(ast.expression(grouped.end)));
}

TEST_CASE("Parser: control-flow braces win over ungrouped construction") {
    const auto result = parse_valid(
        "fn conditions() { while ready {} while (Flag {}) {} "
        "while (fn() -> bool { predicate }) {} }"
    );
    const auto ast = result.view();
    const auto& plain = get<ASTWhileStmt>(statement_at(result, 0));
    CHECK(is<ASTNameExpr>(ast.expression(plain.condition)));
    CHECK(ast.block(plain.body).statements.empty());

    const auto& named = get<ASTWhileStmt>(statement_at(result, 1));
    const auto& named_group = get<ASTGroupExpr>(ast.expression(named.condition));
    CHECK(is<ASTConstructionExpr>(ast.expression(named_group.expression)));

    const auto& typed = get<ASTWhileStmt>(statement_at(result, 2));
    const auto& typed_group = get<ASTGroupExpr>(ast.expression(typed.condition));
    const auto& construction = get<ASTConstructionExpr>(ast.expression(typed_group.expression));
    CHECK(is<ASTFunctionType>(construction.type));

    check_invalid("fn f() { while Flag {} {} }");
}

TEST_CASE("Parser: c_style for alternatives preserve absence and action kind") {
    const auto result = parse_valid(
        "fn loops() { for ; ; {} for index = 0; ready; {} "
        "for begin(); ready; tick() {} }"
    );
    const auto header_at = [&](std::size_t index) noexcept -> const ASTCStyleForHeader& {
        return get<ASTCStyleForHeader>(get<ASTForStmt>(statement_at(result, index)).header);
    };
    CHECK(is<std::monostate>(header_at(0).initializer));
    CHECK(!header_at(0).condition.has_value());
    CHECK(is<ASTAssignment>(header_at(1).initializer));
    CHECK(header_at(1).condition.has_value());
    CHECK(is<ASTExprID>(header_at(2).initializer));
    CHECK(is<ASTExprID>(header_at(2).steps[0]));
}

TEST_CASE("Parser: control transfer uses dedicated alternatives") {
    const auto result = parse_valid("fn transfer() { return value; break; continue; }");
    const auto& returned = get<ASTControlTransfer>(statement_at(result, 0));
    CHECK_EQ(returned.kind, ASTControlTransferKind::Return);
    CHECK(returned.value.has_value());
    const auto& broken = get<ASTControlTransfer>(statement_at(result, 1));
    CHECK_EQ(broken.kind, ASTControlTransferKind::Break);
    CHECK(!broken.value.has_value());
    CHECK(is<ASTControlTransfer>(statement_at(result, 2)));
    check_invalid("fn invalid() { #[cpp] ---\nnative();\n---\n}");
}

TEST_CASE("Parser: builtin names use ordinary calls in every context") {
    static constexpr auto text = std::string_view(
        "test \"context\" {\n"
        " check(true, \"root\");\n"
        " if true { require(true); }\n"
        " let nested = []() { check(true); };\n"
        " let value = check(true);\n"
        " fail();\n"
        "}\n"
        "fn production() { check(true); }\n"
    );
    const auto result = parse_valid(text);
    const auto ast = result.view();
    const auto& test = get<ASTTestDecl>(item(result, 0));
    const auto& body = ast.block(test.body);
    REQUIRE_EQ(body.statements.size(), 5u);

    const auto& root_statement = get<ASTExprStatement>(ast.statement(body.statements[0]));
    const auto& root_operation = get<ASTCallExpr>(ast.expression(root_statement.expression));
    REQUIRE_EQ(root_operation.arguments.size(), 2u);

    const auto& conditional = get<ASTIfForm>(ast.statement(body.statements[1]));
    const auto& branch = ast.branch_block(conditional.branches.front().body);
    REQUIRE_EQ(branch.statements.size(), 1u);
    CHECK(is<ASTExprStatement>(ast.statement(branch.statements.front())));

    const auto& nested = get<ASTVariableDecl>(ast.statement(body.statements[2]));
    REQUIRE(nested.initializer.has_value());
    const auto& lambda = get<ASTLambdaExpr>(ast.expression(*nested.initializer));
    const auto& lambda_statement =
        ast.statement(ast.block(get<ASTBlockID>(lambda.body)).statements.front());
    CHECK(is<ASTExprStatement>(lambda_statement));

    const auto& value = get<ASTVariableDecl>(ast.statement(body.statements[3]));
    REQUIRE(value.initializer.has_value());
    CHECK(is<ASTCallExpr>(ast.expression(*value.initializer)));
    CHECK(is<ASTExprStatement>(ast.statement(body.statements[4])));

    const auto& production = get<ASTFunctionDecl>(item(result, 1));
    const auto& production_body =
        ast.block(get<ASTBlockID>(get<ASTFunctionBody>(production.implementation).body));
    const auto& production_statement = ast.statement(production_body.statements.front());
    CHECK(is<ASTExprStatement>(production_statement));

    check_invalid("test \"block\" { { check(true); } }");
}

TEST_CASE("Parser: malformed c_style for expressions report without crashing") {
    check_invalid("fn loops() { for * ; ready; tick() {} }");
    check_invalid("fn loops() { for value; ready; * {} }");
}

TEST_CASE("Parser: discard targets cover bindings and range loops") {
    static constexpr auto text = std::string_view(
        "fn discard(values: [i32; 2]) {"
        " let _ = 1; var _ = 2; const _ = 3;"
        " let _name = 4; for _ in values {} for &_ in values {}"
        "}"
    );
    const auto result = parse_valid(text);
    REQUIRE_EQ(body_of(result).statements.size(), 6u);
    for (auto index = 0uz; index < 3; ++index) {
        CHECK(
            is<ASTDiscardBindingTarget>(get<ASTVariableDecl>(statement_at(result, index)).target)
        );
    }
    const auto& named = get<ASTVariableDecl>(statement_at(result, 3));
    CHECK(is<ASTNamedBindingTarget>(named.target));
    CHECK_EQ(slice(text, get<ASTNamedBindingTarget>(named.target).name_span), "_name");
    const auto& value_range =
        get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, 4)).header);
    const auto& reference_range =
        get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, 5)).header);
    CHECK(is<ASTDiscardBindingTarget>(value_range.target));
    CHECK(is<ASTDiscardBindingTarget>(reference_range.target));
    CHECK(!value_range.write_marker.has_value());
    CHECK(reference_range.write_marker.has_value());
}
