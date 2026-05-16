import zero.common.source;
import zero.frontend.token;
import zero.frontend.lexer;
import zero.frontend.ast;
import zero.frontend.parser;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

using enum TokenKind;

static auto parse_expr_src(std::string_view source) noexcept -> ParseResult {
    return parse_expr_tokens(tokenize(source), source);
}

static auto root_expr(const ParseResult& r) noexcept -> const Expr* {
    return r.exprs.empty() ? nullptr : r.exprs.back().get();
}

template<typename T>
static auto as(const Expr& expr) noexcept -> const T* {
    return std::get_if<T>(&expr);
}

static auto test_int_literal() noexcept -> void {
    current_test = "int literal";
    auto r = parse_expr_src("42");
    if (const auto* e = root_expr(r)) {
        const auto* lit = as<LiteralExpr>(*e);
        CHECK(lit != nullptr);
        if (lit) CHECK_EQ(text_at("42", lit->token), "42");
    } else { CHECK(false); }
}

static auto test_string_literal() noexcept -> void {
    current_test = "string literal";
    auto r = parse_expr_src(R"("hello")");
    if (const auto* e = root_expr(r)) CHECK(as<LiteralExpr>(*e) != nullptr);
    else CHECK(false);
}

static auto test_identifier() noexcept -> void {
    current_test = "identifier";
    auto r = parse_expr_src("foo");
    if (const auto* e = root_expr(r)) {
        const auto* id = as<IdentExpr>(*e);
        CHECK(id != nullptr);
        if (id) CHECK_EQ(text_at("foo", id->name), "foo");
    } else { CHECK(false); }
}

static auto test_true_false_literals() noexcept -> void {
    current_test = "true/false literals";
    { auto r = parse_expr_src("true"); CHECK(root_expr(r) && as<LiteralExpr>(*root_expr(r))); }
    { auto r = parse_expr_src("false"); CHECK(root_expr(r) && as<LiteralExpr>(*root_expr(r))); }
}

static auto test_simple_binary() noexcept -> void {
    current_test = "simple binary 1 + 2";
    auto r = parse_expr_src("1 + 2");
    if (const auto* e = root_expr(r)) {
        const auto* bin = as<BinaryExpr>(*e);
        CHECK(bin != nullptr);
        if (bin) {
            CHECK_EQ(bin->op, BinOp::Add);
            CHECK(as<LiteralExpr>(*bin->lhs) != nullptr);
            CHECK(as<LiteralExpr>(*bin->rhs) != nullptr);
        }
    } else CHECK(false);
}

static auto test_left_associative() noexcept -> void {
    current_test = "left associative 1 - 2 - 3";
    auto r = parse_expr_src("1 - 2 - 3");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<BinaryExpr>(*e);
        CHECK(outer != nullptr);
        if (outer) {
            CHECK_EQ(outer->op, BinOp::Sub);
            const auto* lhs = as<BinaryExpr>(*outer->lhs);
            CHECK(lhs != nullptr && lhs->op == BinOp::Sub);
        }
    } else CHECK(false);
}

static auto test_precedence_mult_before_add() noexcept -> void {
    current_test = "precedence: * before +";
    auto r = parse_expr_src("1 + 2 * 3");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<BinaryExpr>(*e);
        CHECK(outer != nullptr && outer->op == BinOp::Add);
        if (outer) {
            CHECK(as<LiteralExpr>(*outer->lhs) != nullptr);
            const auto* rhs = as<BinaryExpr>(*outer->rhs);
            CHECK(rhs != nullptr && rhs->op == BinOp::Mul);
        }
    } else CHECK(false);
}

static auto test_comparison_operators() noexcept -> void {
    current_test = "comparison: x < y && a == b";
    auto r = parse_expr_src("x < y && a == b");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<BinaryExpr>(*e);
        CHECK(outer != nullptr && outer->op == BinOp::LogicalAnd);
        if (outer) {
            const auto* lhs = as<BinaryExpr>(*outer->lhs);
            CHECK(lhs != nullptr && lhs->op == BinOp::Lt);
            const auto* rhs = as<BinaryExpr>(*outer->rhs);
            CHECK(rhs != nullptr && rhs->op == BinOp::Eq);
        }
    } else CHECK(false);
}

static auto test_bitwise_precedence() noexcept -> void {
    current_test = "bitwise: a | b & c";
    auto r = parse_expr_src("a | b & c");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<BinaryExpr>(*e);
        CHECK(outer != nullptr && outer->op == BinOp::BitOr);
        if (outer) {
            CHECK(as<IdentExpr>(*outer->lhs) != nullptr);
            const auto* rhs = as<BinaryExpr>(*outer->rhs);
            CHECK(rhs != nullptr && rhs->op == BinOp::BitAnd);
        }
    } else CHECK(false);
}

static auto test_simple_assignment() noexcept -> void {
    current_test = "simple assignment x = 5";
    auto r = parse_expr_src("x = 5");
    if (const auto* e = root_expr(r)) {
        const auto* a = as<AssignExpr>(*e);
        CHECK(a != nullptr);
        if (a) {
            CHECK(as<IdentExpr>(*a->lhs) != nullptr);
            CHECK(as<LiteralExpr>(*a->rhs) != nullptr);
        }
    } else CHECK(false);
}

static auto test_compound_assignment() noexcept -> void {
    current_test = "compound assignment x += 5";
    auto r = parse_expr_src("x += 5");
    if (const auto* e = root_expr(r)) {
        const auto* a = as<CompoundAssignExpr>(*e);
        CHECK(a != nullptr && a->op == BinOp::Add);
    } else CHECK(false);
}

static auto test_right_assoc_assignment() noexcept -> void {
    current_test = "right-assoc assignment a = b = 0";
    auto r = parse_expr_src("a = b = 0");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<AssignExpr>(*e);
        CHECK(outer != nullptr);
        if (outer) {
            CHECK(as<IdentExpr>(*outer->lhs) != nullptr);
            CHECK(as<AssignExpr>(*outer->rhs) != nullptr);
        }
    } else CHECK(false);
}

static auto test_prefix_negation() noexcept -> void {
    current_test = "prefix negation: -x";
    auto r = parse_expr_src("-x");
    if (const auto* e = root_expr(r)) {
        const auto* p = as<PrefixExpr>(*e);
        CHECK(p != nullptr && p->op == UnaryOp::Neg);
        if (p) CHECK(as<IdentExpr>(*p->rhs) != nullptr);
    } else CHECK(false);
}

static auto test_prefix_not() noexcept -> void {
    current_test = "prefix logical not: !true";
    auto r = parse_expr_src("!true");
    if (const auto* e = root_expr(r)) {
        const auto* p = as<PrefixExpr>(*e);
        CHECK(p != nullptr && p->op == UnaryOp::LogicalNot);
    } else CHECK(false);
}

static auto test_prefix_deref() noexcept -> void {
    current_test = "prefix deref: *ptr";
    auto r = parse_expr_src("*ptr");
    if (const auto* e = root_expr(r)) {
        const auto* p = as<PrefixExpr>(*e);
        CHECK(p != nullptr && p->op == UnaryOp::Deref);
    } else CHECK(false);
}

static auto test_prefix_address_of() noexcept -> void {
    current_test = "prefix address-of: &x";
    auto r = parse_expr_src("&x");
    if (const auto* e = root_expr(r)) {
        const auto* p = as<PrefixExpr>(*e);
        CHECK(p != nullptr && p->op == UnaryOp::AddressOf);
    } else CHECK(false);
}

static auto test_prefix_preinc() noexcept -> void {
    current_test = "prefix pre-increment: ++x";
    auto r = parse_expr_src("++x");
    if (const auto* e = root_expr(r)) {
        const auto* p = as<PrefixExpr>(*e);
        CHECK(p != nullptr && p->op == UnaryOp::PreInc);
    } else CHECK(false);
}

static auto test_postfix_inc() noexcept -> void {
    current_test = "postfix increment: x++";
    auto r = parse_expr_src("x++");
    if (const auto* e = root_expr(r)) {
        const auto* p = as<PostfixExpr>(*e);
        CHECK(p != nullptr && p->op == UnaryOp::PostInc);
        if (p) CHECK(as<IdentExpr>(*p->lhs) != nullptr);
    } else CHECK(false);
}

static auto test_function_call_no_args() noexcept -> void {
    current_test = "function call no args: foo()";
    auto r = parse_expr_src("foo()");
    if (const auto* e = root_expr(r)) {
        const auto* c = as<CallExpr>(*e);
        CHECK(c != nullptr && c->args.empty());
        if (c) CHECK(as<IdentExpr>(*c->callee) != nullptr);
    } else CHECK(false);
}

static auto test_function_call_with_args() noexcept -> void {
    current_test = "function call with args: foo(1, x)";
    auto r = parse_expr_src("foo(1, x)");
    if (const auto* e = root_expr(r)) {
        const auto* c = as<CallExpr>(*e);
        CHECK(c != nullptr && c->args.size() == 2);
    } else CHECK(false);
}

static auto test_field_access() noexcept -> void {
    current_test = "field access: x.y";
    auto r = parse_expr_src("x.y");
    if (const auto* e = root_expr(r)) {
        const auto* f = as<FieldExpr>(*e);
        CHECK(f != nullptr);
        if (f) CHECK(as<IdentExpr>(*f->lhs) != nullptr);
    } else CHECK(false);
}

static auto test_scope_access() noexcept -> void {
    current_test = "scope access: std::println";
    auto r = parse_expr_src("std::println");
    if (const auto* e = root_expr(r)) {
        const auto* f = as<FieldExpr>(*e);
        CHECK(f != nullptr);
        if (f) {
            CHECK(as<IdentExpr>(*f->lhs) != nullptr);
            CHECK_EQ(text_at("std::println", f->field), "println");
        }
    } else CHECK(false);
}

static auto test_chained_field() noexcept -> void {
    current_test = "chained field: a.b.c";
    auto r = parse_expr_src("a.b.c");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<FieldExpr>(*e);
        CHECK(outer != nullptr);
        if (outer) CHECK(as<FieldExpr>(*outer->lhs) != nullptr);
    } else CHECK(false);
}

static auto test_index_expr() noexcept -> void {
    current_test = "index: arr[0]";
    auto r = parse_expr_src("arr[0]");
    if (const auto* e = root_expr(r)) {
        const auto* idx = as<IndexExpr>(*e);
        CHECK(idx != nullptr);
        if (idx) {
            CHECK(as<IdentExpr>(*idx->lhs) != nullptr);
            CHECK(as<LiteralExpr>(*idx->index) != nullptr);
        }
    } else CHECK(false);
}

static auto test_group_expr() noexcept -> void {
    current_test = "group: (1 + 2) * 3";
    auto r = parse_expr_src("(1 + 2) * 3");
    if (const auto* e = root_expr(r)) {
        const auto* outer = as<BinaryExpr>(*e);
        CHECK(outer != nullptr && outer->op == BinOp::Mul);
        if (outer) {
            const auto* grp = as<GroupExpr>(*outer->lhs);
            CHECK(grp != nullptr);
            if (grp) CHECK(as<BinaryExpr>(*grp->inner) != nullptr);
        }
    } else CHECK(false);
}

static auto test_comma_expr() noexcept -> void {
    current_test = "comma: a, b";
    auto r = parse_expr_src("a, b");
    const auto* e = root_expr(r);
    CHECK(e && as<CommaExpr>(*e) != nullptr);
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
