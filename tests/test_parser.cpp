#include "test_helpers.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

static auto parse_source(std::string_view source) noexcept -> ParseResult {
    return parse(tokenize(source), source);
}

static auto parse_ok(std::string_view source) noexcept -> ParseResult {
    auto result = parse_source(source);
    CAPTURE(source);
    CHECK(result.errors.empty());
    return result;
}

static auto parse_error(std::string_view source) noexcept -> void {
    const auto result = parse_source(source);
    CAPTURE(source);
    CHECK(!result.errors.empty());
}

template<typename T>
static auto first_item(const ParseResult& result) noexcept -> const T* {
    if (result.items.empty()) return nullptr;
    return std::get_if<T>(&result.items[0]);
}

template<typename T>
static auto item_at(const ParseResult& result, std::size_t index) noexcept -> const T* {
    if (index >= result.items.size()) return nullptr;
    return std::get_if<T>(&result.items[index]);
}

static auto main_body(const ParseResult& result) noexcept -> const BlockStmt* {
    const auto fn = first_item<FunctionItem>(result);
    if (fn == nullptr) return nullptr;
    return fn->body;
}

template<typename T>
static auto first_stmt(const BlockStmt* body, std::size_t index = 0) noexcept -> const T* {
    if (body == nullptr || index >= body->statements.size()) return nullptr;
    const auto stmt = body->statements[index];
    if (stmt == nullptr || stmt->kind != T::kind) return nullptr;
    return static_cast<const T*>(stmt);
}

template<typename T>
static auto expr_stmt(const BlockStmt* body, std::size_t index = 0) noexcept -> const T* {
    const auto stmt = first_stmt<ExprStmt>(body, index);
    if (stmt == nullptr || stmt->expr == nullptr || stmt->expr->kind != T::kind) return nullptr;
    return static_cast<const T*>(stmt->expr);
}

template<typename T>
static auto type_as(const Type* type) noexcept -> const T* {
    if (type == nullptr || type->kind != T::kind) return nullptr;
    return static_cast<const T*>(type);
}

template<typename T>
static auto expr_as(const Expr* expr) noexcept -> const T* {
    if (expr == nullptr || expr->kind != T::kind) return nullptr;
    return static_cast<const T*>(expr);
}

TEST_CASE("Parser: valid grammar snippets") {
    constexpr auto snippets = std::array<std::string_view, 30> {
        "",
        "import std;",
        "import std using println",
        "import std using { println, format }",
        "import std using *",
        "enum Color : u8 { Red, Green, }",
        "enum Empty {}",
        "struct Point { x: i32, y: i32 }",
        "struct Pair { left: i32; right: i32 }",
        "fn main() { }",
        "fn main(args) { }",
        "fn add(a: i32, b: i32) -> i32 { return a + b; }",
        "fn generic(value) { return value; }",
        "fn main() { let x: i32 = 1; var y: i32; const N = 2; }",
        "fn main() { while true return; }",
        "fn main() { while x > 0 { x--; } }",
        "fn main() { for ( ; ; ) { } }",
        "fn main() { for (var i = 0; i < 10; i++) { } }",
        "fn main() { if true { return; } }",
        "fn main() { if true { } else { } }",
        "fn main() { 42; 3.14; \"hello\"; 'a'; true; false; }",
        "fn main() { -x; !flag; ~mask; ++i; &x; *p; i++; }",
        "fn main() { foo(1, 2)[0].field; ns::value; ptr->field; }",
        "fn main() { (a + b), c; }",
        "fn main() { a = b = c; a += b; }",
        "fn main() { let x = if true { 1 } else { 2 }; }",
        "fn main() { let a = [1, 2, 3,]; let m = [[1], [2]]; }",
        "fn main() { let a: [i32; 3] = [1, 2, 3]; }",
        "fn main() { match x { 1 | 2 => { a; } _ => { b; } } }",
        "fn main() { let y = match x { i32 | f64 => { 1 } _ => { 0 } }; }",
    };

    for (const auto source : snippets) {
        const auto result = parse_source(source);
        CAPTURE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: imports and top-level items") {
    SUBCASE("import std using list") {
        constexpr auto source = "import std using { println, format }";
        const auto result = parse_ok(source);
        const auto item = first_item<ImportItem>(result);

        REQUIRE(item != nullptr);
        CHECK_EQ(slice(source, item->module_name), "std");
        CHECK(item->is_std_module);
        REQUIRE_EQ(item->using_decls.size(), 2u);
        CHECK_EQ(slice(source, item->using_decls[0]), "println");
        CHECK_EQ(slice(source, item->using_decls[1]), "format");
    }

    SUBCASE("enum, struct, and function") {
        constexpr auto source =
            "enum Color : u8 { Red, Green }\n"
            "struct Point { x: i32, y: i32 }\n"
            "fn add(a: i32, b) -> i32 { return a; }";
        const auto result = parse_ok(source);

        REQUIRE_EQ(result.items.size(), 3u);

        const auto enum_item = item_at<EnumItem>(result, 0);
        REQUIRE(enum_item != nullptr);
        CHECK(type_as<NameType>(enum_item->size) != nullptr);
        CHECK_EQ(enum_item->fields.size(), 2u);

        const auto struct_item = item_at<StructItem>(result, 1);
        REQUIRE(struct_item != nullptr);
        CHECK_EQ(struct_item->fields.size(), 2u);

        const auto fn = item_at<FunctionItem>(result, 2);
        REQUIRE(fn != nullptr);
        CHECK_EQ(fn->params.size(), 2u);
        CHECK(fn->params[0].type != nullptr);
        CHECK(fn->params[1].type == nullptr);
        CHECK(fn->return_type != nullptr);
    }
}

TEST_CASE("Parser: main args parameter") {
    constexpr auto source = "fn main(args) { }";
    const auto result = parse_ok(source);
    const auto fn = first_item<FunctionItem>(result);

    REQUIRE(fn != nullptr);
    REQUIRE_EQ(fn->params.size(), 1u);
    CHECK_EQ(slice(source, fn->params[0].name), "args");
    CHECK(fn->params[0].type == nullptr);
}

TEST_CASE("Parser: declarations, loops, and expressions") {
    SUBCASE("declarations retain type/init shape") {
        constexpr auto source = "fn main() { let x: i32 = 1; var y: i32; const N = 2; }";
        const auto result = parse_ok(source);
        const auto body = main_body(result);

        REQUIRE(body != nullptr);
        REQUIRE_EQ(body->statements.size(), 3u);

        const auto let_decl = first_stmt<VarDecl>(body, 0);
        const auto var_decl = first_stmt<VarDecl>(body, 1);
        const auto const_decl = first_stmt<VarDecl>(body, 2);
        REQUIRE(let_decl != nullptr);
        REQUIRE(var_decl != nullptr);
        REQUIRE(const_decl != nullptr);

        CHECK_EQ(slice(source, let_decl->keyword), "let");
        CHECK(type_as<NameType>(let_decl->type) != nullptr);
        CHECK(let_decl->init != nullptr);
        CHECK_EQ(slice(source, var_decl->keyword), "var");
        CHECK(type_as<NameType>(var_decl->type) != nullptr);
        CHECK(var_decl->init == nullptr);
        CHECK_EQ(slice(source, const_decl->keyword), "const");
        CHECK(const_decl->init != nullptr);
    }

    SUBCASE("while and for statements") {
        constexpr auto source = "fn main() { while true return; for (var i = 0; i < 10; i++) { } }";
        const auto result = parse_ok(source);
        const auto body = main_body(result);

        const auto loop = first_stmt<WhileStmt>(body, 0);
        REQUIRE(loop != nullptr);
        REQUIRE(loop->body != nullptr);
        CHECK_EQ(loop->body->kind, StmtKind::Return);

        const auto for_stmt = first_stmt<ForStmt>(body, 1);
        REQUIRE(for_stmt != nullptr);
        CHECK(std::visit([](auto value) static noexcept -> bool { return value != nullptr; }, for_stmt->init));
        CHECK(for_stmt->condition != nullptr);
        CHECK(for_stmt->step != nullptr);
    }

    SUBCASE("postfix chain") {
        constexpr auto source = "fn main() { foo(1, 2)[0].field++; }";
        const auto result = parse_ok(source);
        const auto expr = expr_stmt<PostfixExpr>(main_body(result));
        REQUIRE(expr != nullptr);
        CHECK_EQ(expr->op, UnaryOp::PostInc);
        CHECK(expr_as<FieldExpr>(expr->lhs) != nullptr);
    }

    SUBCASE("assignment is right-associative") {
        constexpr auto source = "fn main() { a = b = c; }";
        const auto result = parse_ok(source);
        const auto assign = expr_stmt<AssignExpr>(main_body(result));
        REQUIRE(assign != nullptr);
        CHECK(expr_as<AssignExpr>(assign->rhs) != nullptr);
    }

    SUBCASE("multiplicative binds tighter than additive") {
        constexpr auto source = "fn main() { a + b * c; }";
        const auto result = parse_ok(source);
        const auto bin = expr_stmt<BinaryExpr>(main_body(result));
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Add);
        const auto rhs = expr_as<BinaryExpr>(bin->rhs);
        REQUIRE(rhs != nullptr);
        CHECK_EQ(rhs->op, BinOp::Mul);
    }
}

TEST_CASE("Parser: if, match, and arrays") {
    SUBCASE("value if in declaration") {
        constexpr auto source = "fn main() { let x = if true { 1 } else { 2 }; }";
        const auto result = parse_ok(source);
        const auto decl = first_stmt<VarDecl>(main_body(result));
        REQUIRE(decl != nullptr);
        const auto if_expr = expr_as<IfExpr>(decl->init);
        REQUIRE(if_expr != nullptr);
        CHECK(if_expr->else_branch != nullptr);
    }

    SUBCASE("match expression arms") {
        constexpr auto source = "fn main() { let y = match x { 1 | 2 => { 10 } _ => { 0 } }; }";
        const auto result = parse_ok(source);
        const auto decl = first_stmt<VarDecl>(main_body(result));
        REQUIRE(decl != nullptr);
        const auto match = expr_as<MatchExpr>(decl->init);
        REQUIRE(match != nullptr);
        REQUIRE_EQ(match->arms.size(), 2u);
        CHECK_EQ(match->arms[0].patterns.size(), 2u);
        CHECK(match->arms[1].is_wildcard);
    }

    SUBCASE("array literal and type annotation") {
        constexpr auto source = "fn main() { let a: [i32; 3] = [1, 2, 3]; }";
        const auto result = parse_ok(source);
        const auto decl = first_stmt<VarDecl>(main_body(result));
        REQUIRE(decl != nullptr);

        const auto array_type = type_as<ArrayType>(decl->type);
        REQUIRE(array_type != nullptr);
        CHECK_EQ(slice(source, array_type->elem_type), "i32");
        CHECK_EQ(slice(source, array_type->size), "3");

        const auto array_expr = expr_as<ArrayExpr>(decl->init);
        REQUIRE(array_expr != nullptr);
        CHECK_EQ(array_expr->elements.size(), 3u);
    }
}

TEST_CASE("Parser: errors and recovery") {
    constexpr auto invalid_snippets = std::array<std::string_view, 6> {
        "fn main() { let x: i32; }",
        "fn main() { const x: i32; }",
        "let x = 1;",
        "fn main() { @ invalid @; return; }",
        "fn main() { match x 1 => { y; } }",
        "fn main() { match x { 1 { y; } } }",
    };

    for (const auto source : invalid_snippets) {
        parse_error(source);
    }

    SUBCASE("statement recovery keeps following statement") {
        constexpr auto source = "fn main() { @ invalid @; return; }";
        const auto result = parse_source(source);
        CHECK(!result.errors.empty());
        const auto body = main_body(result);
        REQUIRE(body != nullptr);
        CHECK(!body->statements.empty());
    }

    SUBCASE("top-level recovery keeps following function") {
        constexpr auto source = "@ bad top level\nfn main() { }";
        const auto result = parse_source(source);
        CHECK(!result.errors.empty());
        REQUIRE_EQ(result.items.size(), 1u);
        CHECK(first_item<FunctionItem>(result) != nullptr);
    }
}
