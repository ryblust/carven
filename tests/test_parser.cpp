import zero.frontend.token;
import zero.frontend.lexer;
import zero.frontend.ast;
import zero.frontend.parser;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)

using enum TokenKind;

static auto expr_of(ParseResult& r, std::string_view code) noexcept -> const Expr* {
    auto src = std::string("fn __t() { ") + std::string(code) + "; }";
    r = parse(tokenize(src), src);
    const auto* fn = std::get_if<FunctionItem>(&r.items[0]);
    if (!fn || fn->body->statements.empty()) return nullptr;
    const auto* es = std::get_if<ExprStmt>(fn->body->statements[0]);
    return es ? es->expr : nullptr;
}

static auto test_int_literal() noexcept -> void {
    current_test = "int literal";
    ParseResult r;
    const auto* e = expr_of(r, "42");
    CHECK(e && std::get_if<LiteralExpr>(e));
}

static auto test_string_literal() noexcept -> void {
    current_test = "string literal";
    ParseResult r;
    const auto* e = expr_of(r, R"("hello")");
    CHECK(e && std::get_if<LiteralExpr>(e));
}

static auto test_identifier() noexcept -> void {
    current_test = "identifier";
    ParseResult r;
    const auto* e = expr_of(r, "foo");
    CHECK(e && std::get_if<IdentExpr>(e));
}

static auto test_true_false_literals() noexcept -> void {
    current_test = "true/false literals";
    { ParseResult r; CHECK(expr_of(r, "true") && std::get_if<LiteralExpr>(expr_of(r, "true"))); }
    { ParseResult r; CHECK(expr_of(r, "false") && std::get_if<LiteralExpr>(expr_of(r, "false"))); }
}

static auto test_simple_binary() noexcept -> void {
    current_test = "simple binary 1 + 2";
    ParseResult r;
    const auto* e = expr_of(r, "1 + 2");
    const auto* bin = std::get_if<BinaryExpr>(e);
    CHECK(bin && bin->op == BinOp::Add);
}

static auto test_left_associative() noexcept -> void {
    current_test = "left associative 1 - 2 - 3";
    ParseResult r;
    const auto* e = expr_of(r, "1 - 2 - 3");
    const auto* outer = std::get_if<BinaryExpr>(e);
    CHECK(outer && outer->op == BinOp::Sub && std::get_if<BinaryExpr>(outer->lhs));
}

static auto test_precedence_mult_before_add() noexcept -> void {
    current_test = "precedence: * before +";
    ParseResult r;
    const auto* e = expr_of(r, "1 + 2 * 3");
    const auto* outer = std::get_if<BinaryExpr>(e);
    CHECK(outer && outer->op == BinOp::Add && std::get_if<BinaryExpr>(outer->rhs));
}

static auto test_comparison_operators() noexcept -> void {
    current_test = "comparison: x < y && a == b";
    ParseResult r;
    const auto* e = expr_of(r, "x < y && a == b");
    const auto* outer = std::get_if<BinaryExpr>(e);
    CHECK(outer && outer->op == BinOp::LogicalAnd);
}

static auto test_bitwise_precedence() noexcept -> void {
    current_test = "bitwise: a | b & c";
    ParseResult r;
    const auto* e = expr_of(r, "a | b & c");
    const auto* outer = std::get_if<BinaryExpr>(e);
    CHECK(outer && outer->op == BinOp::BitOr && std::get_if<BinaryExpr>(outer->rhs));
}

static auto test_simple_assignment() noexcept -> void {
    current_test = "simple assignment x = 5";
    ParseResult r;
    const auto* e = expr_of(r, "x = 5");
    const auto* a = std::get_if<AssignExpr>(e);
    CHECK(a && std::get_if<IdentExpr>(a->lhs));
}

static auto test_compound_assignment() noexcept -> void {
    current_test = "compound assignment x += 5";
    ParseResult r;
    const auto* e = expr_of(r, "x += 5");
    const auto* a = std::get_if<CompoundAssignExpr>(e);
    CHECK(a && a->op == BinOp::Add);
}

static auto test_right_assoc_assignment() noexcept -> void {
    current_test = "right-assoc assignment a = b = 0";
    ParseResult r;
    const auto* e = expr_of(r, "a = b = 0");
    const auto* outer = std::get_if<AssignExpr>(e);
    CHECK(outer && std::get_if<AssignExpr>(outer->rhs));
}

static auto test_prefix_negation() noexcept -> void {
    current_test = "prefix negation: -x";
    ParseResult r;
    const auto* e = expr_of(r, "-x");
    const auto* p = std::get_if<PrefixExpr>(e);
    CHECK(p && p->op == UnaryOp::Neg);
}

static auto test_prefix_not() noexcept -> void {
    current_test = "prefix logical not: !true";
    ParseResult r;
    const auto* e = expr_of(r, "!true");
    const auto* p = std::get_if<PrefixExpr>(e);
    CHECK(p && p->op == UnaryOp::LogicalNot);
}

static auto test_prefix_deref() noexcept -> void {
    current_test = "prefix deref: *ptr";
    ParseResult r;
    const auto* e = expr_of(r, "*ptr");
    const auto* p = std::get_if<PrefixExpr>(e);
    CHECK(p && p->op == UnaryOp::Deref);
}

static auto test_prefix_address_of() noexcept -> void {
    current_test = "prefix address-of: &x";
    ParseResult r;
    const auto* e = expr_of(r, "&x");
    const auto* p = std::get_if<PrefixExpr>(e);
    CHECK(p && p->op == UnaryOp::AddressOf);
}

static auto test_prefix_preinc() noexcept -> void {
    current_test = "prefix pre-increment: ++x";
    ParseResult r;
    const auto* e = expr_of(r, "++x");
    const auto* p = std::get_if<PrefixExpr>(e);
    CHECK(p && p->op == UnaryOp::PreInc);
}

static auto test_postfix_inc() noexcept -> void {
    current_test = "postfix increment: x++";
    ParseResult r;
    const auto* e = expr_of(r, "x++");
    const auto* p = std::get_if<PostfixExpr>(e);
    CHECK(p && p->op == UnaryOp::PostInc);
}

static auto test_function_call_no_args() noexcept -> void {
    current_test = "function call no args: foo()";
    ParseResult r;
    const auto* e = expr_of(r, "foo()");
    const auto* c = std::get_if<CallExpr>(e);
    CHECK(c && c->args.empty());
}

static auto test_function_call_with_args() noexcept -> void {
    current_test = "function call with args: foo(1, x)";
    ParseResult r;
    const auto* e = expr_of(r, "foo(1, x)");
    const auto* c = std::get_if<CallExpr>(e);
    CHECK(c && c->args.size() == 2);
}

static auto test_field_access() noexcept -> void {
    current_test = "field access: x.y";
    ParseResult r;
    const auto* e = expr_of(r, "x.y");
    CHECK(std::get_if<FieldExpr>(e));
}

static auto test_scope_access() noexcept -> void {
    current_test = "scope access: std::println";
    ParseResult r;
    const auto* e = expr_of(r, "std::println");
    CHECK(std::get_if<FieldExpr>(e));
}

static auto test_chained_field() noexcept -> void {
    current_test = "chained field: a.b.c";
    ParseResult r;
    const auto* e = expr_of(r, "a.b.c");
    const auto* outer = std::get_if<FieldExpr>(e);
    CHECK(outer && std::get_if<FieldExpr>(outer->lhs));
}

static auto test_index_expr() noexcept -> void {
    current_test = "index: arr[0]";
    ParseResult r;
    const auto* e = expr_of(r, "arr[0]");
    CHECK(std::get_if<IndexExpr>(e));
}

static auto test_group_expr() noexcept -> void {
    current_test = "group: (1 + 2) * 3";
    ParseResult r;
    const auto* e = expr_of(r, "(1 + 2) * 3");
    const auto* outer = std::get_if<BinaryExpr>(e);
    CHECK(outer && outer->op == BinOp::Mul && std::get_if<GroupExpr>(outer->lhs));
}

static auto test_comma_expr() noexcept -> void {
    current_test = "comma: a, b";
    ParseResult r;
    const auto* e = expr_of(r, "a, b");
    CHECK(std::get_if<CommaExpr>(e));
}

static auto test_parse_empty_function() noexcept -> void {
    current_test = "parse empty function";
    auto result = parse(tokenize("fn main() {}"), "fn main() {}");
    CHECK(!result.has_errors() && result.items.size() == 1);
}

static auto test_parse_function_with_return() noexcept -> void {
    current_test = "parse function: return";
    auto result = parse(tokenize("fn main() { return; }"), "fn main() { return; }");
    CHECK(!result.has_errors() && result.items.size() == 1);
}

static auto test_parse_hello_world() noexcept -> void {
    current_test = "parse helloworld.zero";
    auto src = "fn main() {\n    std::println(\"Hello World\");\n}";
    auto result = parse(tokenize(src), src);
    CHECK(!result.has_errors() && result.items.size() == 1);
}

auto main() noexcept -> int {
    test_int_literal();
    test_string_literal();
    test_identifier();
    test_true_false_literals();
    test_simple_binary();
    test_left_associative();
    test_precedence_mult_before_add();
    test_comparison_operators();
    test_bitwise_precedence();
    test_simple_assignment();
    test_compound_assignment();
    test_right_assoc_assignment();
    test_prefix_negation();
    test_prefix_not();
    test_prefix_deref();
    test_prefix_address_of();
    test_prefix_preinc();
    test_postfix_inc();
    test_function_call_no_args();
    test_function_call_with_args();
    test_field_access();
    test_scope_access();
    test_chained_field();
    test_index_expr();
    test_group_expr();
    test_comma_expr();
    test_parse_empty_function();
    test_parse_function_with_return();
    test_parse_hello_world();

    return print_test_result();
}
