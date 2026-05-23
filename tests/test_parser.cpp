#include "doctest.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

// Helper: parse source and return result
static constexpr auto do_parse(std::string_view source) noexcept -> ParseResult {
    return parse(tokenize(source), source);
}

// ============================================================
// Imports
// ============================================================

TEST_CASE("Parser: import") {
    SUBCASE("basic import") {
        static constexpr auto source = std::string_view("import std;");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        CHECK_EQ(result.items.size(), 1u);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(text_at(source, item->module_name), "std");
        CHECK(item->is_std_module);
    }

    SUBCASE("import without semicolon") {
        static constexpr auto source = std::string_view("import foo");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        CHECK_EQ(result.items.size(), 1u);
    }

    SUBCASE("import with using declaration") {
        static constexpr auto source = std::string_view("import std using println");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->using_decls.size(), 1u);
        CHECK_EQ(text_at(source, item->using_decls[0]), "println");
    }

    SUBCASE("import with using wildcard") {
        static constexpr auto source = std::string_view("import std using *");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->using_wildcard);
    }

    SUBCASE("import with using brace list") {
        static constexpr auto source = std::string_view("import std using { println, format }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->using_decls.size(), 2u);
        CHECK_EQ(text_at(source, item->using_decls[0]), "println");
        CHECK_EQ(text_at(source, item->using_decls[1]), "format");
    }

    SUBCASE("import std auto-detection") {
        static constexpr auto source = std::string_view("import std;");
        const auto result = do_parse(source);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->is_std_module);
    }

    SUBCASE("non-std module not auto-detected") {
        static constexpr auto source = std::string_view("import other;");
        const auto result = do_parse(source);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(!item->is_std_module);
    }
}

// ============================================================
// Enums
// ============================================================

TEST_CASE("Parser: enum") {
    SUBCASE("basic enum") {
        static constexpr auto source = std::string_view("enum Color { Red, Green, Blue }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(text_at(source, item->name), "Color");
        CHECK_EQ(item->fields.size(), 3u);
        CHECK_EQ(text_at(source, item->fields[0]), "Red");
        CHECK_EQ(text_at(source, item->fields[1]), "Green");
        CHECK_EQ(text_at(source, item->fields[2]), "Blue");
    }

    SUBCASE("enum with size type") {
        static constexpr auto source = std::string_view("enum Color : u8 { Red, Green }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(text_at(source, item->size), "u8");
        CHECK_EQ(item->fields.size(), 2u);
    }

    SUBCASE("single field enum") {
        static constexpr auto source = std::string_view("enum Flag { Active }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 1u);
    }

    SUBCASE("empty enum") {
        static constexpr auto source = std::string_view("enum Void {}");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->fields.empty());
    }
}

// ============================================================
// Structs
// ============================================================

TEST_CASE("Parser: struct") {
    SUBCASE("basic struct") {
        static constexpr auto source = std::string_view("struct Point { x: i32, y: i32 }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(text_at(source, item->name), "Point");
        CHECK_EQ(item->fields.size(), 2u);
        CHECK_EQ(text_at(source, item->fields[0].name), "x");
        CHECK_EQ(text_at(source, item->fields[0].type), "i32");
        CHECK_EQ(text_at(source, item->fields[1].name), "y");
        CHECK_EQ(text_at(source, item->fields[1].type), "i32");
    }

    SUBCASE("struct with semicolon separators") {
        static constexpr auto source = std::string_view("struct Foo { a: i32; b: i32 }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 2u);
    }

    SUBCASE("single field struct") {
        static constexpr auto source = std::string_view("struct Wrapper { value: i32 }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 1u);
    }
}

// ============================================================
// Functions
// ============================================================

TEST_CASE("Parser: function") {
    SUBCASE("basic function") {
        static constexpr auto source = std::string_view("fn main() { }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(text_at(source, item->name), "main");
        CHECK(item->params.empty());
        CHECK(is_empty(item->return_type));
    }

    SUBCASE("function with return type") {
        static constexpr auto source = std::string_view("fn add() -> i32 { }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(text_at(source, item->return_type), "i32");
    }

    SUBCASE("function with typed parameters") {
        static constexpr auto source = std::string_view("fn add(a: i32, b: i32) -> i32 { }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->params.size(), 2u);
        CHECK_EQ(text_at(source, item->params[0].name), "a");
        CHECK_EQ(text_at(source, item->params[0].type), "i32");
        CHECK_EQ(text_at(source, item->params[1].name), "b");
        CHECK_EQ(text_at(source, item->params[1].type), "i32");
    }

    SUBCASE("function with untyped parameters") {
        static constexpr auto source = std::string_view("fn foo(x, y) { }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->params.size(), 2u);
        CHECK(is_empty(item->params[0].type));
        CHECK(is_empty(item->params[1].type));
    }

    SUBCASE("function with body statements") {
        static constexpr auto source = std::string_view("fn main() { return 0; }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->body->statements.size(), 1u);
    }
}

// ============================================================
// Variable Declarations
// ============================================================

TEST_CASE("Parser: variable declarations") {
    SUBCASE("let with type and init") {
        static constexpr auto source = std::string_view("fn main() { let x: i32 = 5; }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(text_at(source, decl->keyword), "let");
        CHECK_EQ(text_at(source, decl->name), "x");
        CHECK_EQ(text_at(source, decl->type), "i32");
        CHECK(!is_empty(decl->eq));
    }

    SUBCASE("var with init") {
        static constexpr auto source = std::string_view("fn main() { var y = 10; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(text_at(source, decl->keyword), "var");
        CHECK_EQ(text_at(source, decl->name), "y");
    }

    SUBCASE("var without init") {
        static constexpr auto source = std::string_view("fn main() { var y: i32; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(text_at(source, decl->keyword), "var");
        CHECK_EQ(text_at(source, decl->type), "i32");
        CHECK(decl->init == nullptr);
    }

    SUBCASE("const with init") {
        static constexpr auto source = std::string_view("fn main() { const N = 42; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(text_at(source, decl->keyword), "const");
    }

    SUBCASE("let without init produces error") {
        static constexpr auto source = std::string_view("fn main() { let x: i32; }");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
    }

    SUBCASE("const without init produces error") {
        static constexpr auto source = std::string_view("fn main() { const x: i32; }");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
    }
}

// ============================================================
// Return Statements
// ============================================================

TEST_CASE("Parser: return") {
    SUBCASE("return void") {
        static constexpr auto source = std::string_view("fn main() { return; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ret = std::get_if<ReturnStmt>(fn->body->statements[0]);
        CHECK(ret != nullptr);
        CHECK(ret->value == nullptr);
    }

    SUBCASE("return with value") {
        static constexpr auto source = std::string_view("fn main() { return 42; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ret = std::get_if<ReturnStmt>(fn->body->statements[0]);
        CHECK(ret != nullptr);
        CHECK(ret->value != nullptr);
    }
}

// ============================================================
// While Statements
// ============================================================

TEST_CASE("Parser: while") {
    SUBCASE("while with block body") {
        static constexpr auto source = std::string_view("fn main() { while true { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<WhileStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::get_if<BlockStmt>(loop->body) != nullptr);
    }

    SUBCASE("while with single statement body") {
        static constexpr auto source = std::string_view("fn main() { while true return; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<WhileStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::get_if<ReturnStmt>(loop->body) != nullptr);
    }

    SUBCASE("while with condition expression") {
        static constexpr auto source = std::string_view("fn main() { while x > 0 { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<WhileStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->condition != nullptr);
    }
}

// ============================================================
// For Statements
// ============================================================

TEST_CASE("Parser: for") {
    SUBCASE("full for loop") {
        static constexpr auto source = std::string_view("fn main() { for (var i = 0; i < 10; i++) { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init != nullptr);
        CHECK(loop->condition != nullptr);
        CHECK(loop->step != nullptr);
    }

    SUBCASE("for with empty clauses") {
        static constexpr auto source = std::string_view("fn main() { for ( ; ; ) { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init == nullptr);
        CHECK(loop->condition == nullptr);
        CHECK(loop->step == nullptr);
    }

    SUBCASE("for with let init") {
        static constexpr auto source = std::string_view("fn main() { for (let i = 0; i < 10; i++) { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init != nullptr);
    }

    SUBCASE("for with expr init") {
        static constexpr auto source = std::string_view("fn main() { for (i = 0; i < 10; i++) { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init != nullptr);
    }
}

// ============================================================
// If Expressions
// ============================================================

TEST_CASE("Parser: if expression") {
    SUBCASE("if without else") {
        static constexpr auto source = std::string_view("fn main() { if true { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto expr_stmt = std::get_if<ExprStmt>(fn->body->statements[0]);
        CHECK(expr_stmt != nullptr);
        const auto if_expr = std::get_if<IfExpr>(expr_stmt->expr);
        CHECK(if_expr != nullptr);
        CHECK(!if_expr->else_kw.has_value());
    }

    SUBCASE("if with else") {
        static constexpr auto source = std::string_view("fn main() { if true { } else { } }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto expr_stmt = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto if_expr = std::get_if<IfExpr>(expr_stmt->expr);
        CHECK(if_expr != nullptr);
        CHECK(if_expr->else_kw.has_value());
        CHECK(if_expr->else_branch.has_value());
    }
}

// ============================================================
// Expressions — Literals
// ============================================================

TEST_CASE("Parser: literal expressions") {
    SUBCASE("integer literal") {
        static constexpr auto source = std::string_view("fn main() { 42; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(text_at(source, lit->token), "42");
    }

    SUBCASE("float literal") {
        static constexpr auto source = std::string_view("fn main() { 3.14; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(text_at(source, lit->token), "3.14");
    }

    SUBCASE("string literal") {
        static constexpr auto source = std::string_view("fn main() { \"hello\"; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(text_at(source, lit->token), "\"hello\"");
    }

    SUBCASE("char literal") {
        static constexpr auto source = std::string_view("fn main() { 'a'; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(text_at(source, lit->token), "'a'");
    }

    SUBCASE("bool literals") {
        static constexpr auto source = std::string_view("fn main() { true; false; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es1 = std::get_if<ExprStmt>(fn->body->statements[0]);
        CHECK(std::get_if<LiteralExpr>(es1->expr) != nullptr);
        const auto es2 = std::get_if<ExprStmt>(fn->body->statements[1]);
        CHECK(std::get_if<LiteralExpr>(es2->expr) != nullptr);
    }
}

// ============================================================
// Expressions — Identifiers, Prefix, Postfix
// ============================================================

TEST_CASE("Parser: identifier and unary expressions") {
    SUBCASE("identifier") {
        static constexpr auto source = std::string_view("fn main() { foo; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ident = std::get_if<IdentExpr>(es->expr);
        CHECK(ident != nullptr);
        CHECK_EQ(text_at(source, ident->name), "foo");
    }

    SUBCASE("prefix negation") {
        static constexpr auto source = std::string_view("fn main() { -x; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::Neg);
    }

    SUBCASE("prefix logical not") {
        static constexpr auto source = std::string_view("fn main() { !flag; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::LogicalNot);
    }

    SUBCASE("prefix bitwise not") {
        static constexpr auto source = std::string_view("fn main() { ~mask; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::BitNot);
    }

    SUBCASE("prefix pre-increment") {
        static constexpr auto source = std::string_view("fn main() { ++i; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::PreInc);
    }

    SUBCASE("prefix pre-decrement") {
        static constexpr auto source = std::string_view("fn main() { --i; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::PreDec);
    }

    SUBCASE("prefix address-of") {
        static constexpr auto source = std::string_view("fn main() { &x; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::AddressOf);
    }

    SUBCASE("prefix dereference") {
        static constexpr auto source = std::string_view("fn main() { *p; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::Deref);
    }

    SUBCASE("postfix increment") {
        static constexpr auto source = std::string_view("fn main() { i++; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto post = std::get_if<PostfixExpr>(es->expr);
        CHECK(post != nullptr);
        CHECK_EQ(post->op, UnaryOp::PostInc);
    }

    SUBCASE("postfix decrement") {
        static constexpr auto source = std::string_view("fn main() { i--; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto post = std::get_if<PostfixExpr>(es->expr);
        CHECK(post != nullptr);
        CHECK_EQ(post->op, UnaryOp::PostDec);
    }
}

// ============================================================
// Expressions — Call, Index, Field
// ============================================================

TEST_CASE("Parser: postfix operations") {
    SUBCASE("function call with no args") {
        static constexpr auto source = std::string_view("fn main() { foo(); }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto call = std::get_if<CallExpr>(es->expr);
        CHECK(call != nullptr);
        CHECK(call->args.empty());
    }

    SUBCASE("function call with one arg") {
        static constexpr auto source = std::string_view("fn main() { foo(1); }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto call = std::get_if<CallExpr>(es->expr);
        CHECK(call != nullptr);
        CHECK_EQ(call->args.size(), 1u);
    }

    SUBCASE("function call with multiple args") {
        static constexpr auto source = std::string_view("fn main() { add(1, 2, 3); }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto call = std::get_if<CallExpr>(es->expr);
        CHECK(call != nullptr);
        CHECK_EQ(call->args.size(), 3u);
    }

    SUBCASE("index expression") {
        static constexpr auto source = std::string_view("fn main() { arr[0]; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto idx = std::get_if<IndexExpr>(es->expr);
        CHECK(idx != nullptr);
    }

    SUBCASE("dot field access") {
        static constexpr auto source = std::string_view("fn main() { obj.field; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto field = std::get_if<FieldExpr>(es->expr);
        CHECK(field != nullptr);
        CHECK_EQ(text_at(source, field->dot), ".");
        CHECK_EQ(text_at(source, field->field), "field");
    }

    SUBCASE("arrow field access") {
        static constexpr auto source = std::string_view("fn main() { ptr->field; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto field = std::get_if<FieldExpr>(es->expr);
        CHECK(field != nullptr);
        CHECK_EQ(text_at(source, field->dot), "->");
    }

    SUBCASE("scope access") {
        static constexpr auto source = std::string_view("fn main() { ns::name; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto field = std::get_if<FieldExpr>(es->expr);
        CHECK(field != nullptr);
        CHECK_EQ(text_at(source, field->dot), "::");
    }
}

// ============================================================
// Expressions — Binary Operators (all precedence levels)
// ============================================================

TEST_CASE("Parser: binary expressions") {
    SUBCASE("multiplicative: multiply") {
        static constexpr auto source = std::string_view("fn main() { a * b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Mul);
    }

    SUBCASE("multiplicative: divide") {
        static constexpr auto source = std::string_view("fn main() { a / b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Div);
    }

    SUBCASE("multiplicative: modulo") {
        static constexpr auto source = std::string_view("fn main() { a % b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Mod);
    }

    SUBCASE("additive: plus and minus") {
        static constexpr auto source = std::string_view("fn main() { a + b - c; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Sub);
    }

    SUBCASE("shift: left and right") {
        static constexpr auto source = std::string_view("fn main() { a << b >> c; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Shr);
    }

    SUBCASE("relational: less, greater, le, ge") {
        static constexpr auto source = std::string_view("fn main() { a < b; b > c; c <= d; d >= e; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es1 = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto es2 = std::get_if<ExprStmt>(fn->body->statements[1]);
        const auto es3 = std::get_if<ExprStmt>(fn->body->statements[2]);
        const auto es4 = std::get_if<ExprStmt>(fn->body->statements[3]);
        const auto bin_expr_lt = std::get_if<BinaryExpr>(es1->expr);
        const auto bin_expr_gt = std::get_if<BinaryExpr>(es2->expr);
        const auto bin_expr_le = std::get_if<BinaryExpr>(es3->expr);
        const auto bin_expr_ge = std::get_if<BinaryExpr>(es4->expr);
        REQUIRE(bin_expr_lt != nullptr);
        REQUIRE(bin_expr_gt != nullptr);
        REQUIRE(bin_expr_le != nullptr);
        REQUIRE(bin_expr_ge != nullptr);
        CHECK_EQ(bin_expr_lt->op, BinOp::Lt);
        CHECK_EQ(bin_expr_gt->op, BinOp::Gt);
        CHECK_EQ(bin_expr_le->op, BinOp::Le);
        CHECK_EQ(bin_expr_ge->op, BinOp::Ge);
    }

    SUBCASE("equality: eq and ne") {
        static constexpr auto source = std::string_view("fn main() { a == b; c != d; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        // a == b
        const auto es1 = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin_expr_eq = std::get_if<BinaryExpr>(es1->expr);
        REQUIRE(bin_expr_eq != nullptr);
        CHECK_EQ(bin_expr_eq->op, BinOp::Eq);
        // c != d
        const auto es2 = std::get_if<ExprStmt>(fn->body->statements[1]);
        const auto bin_expr_ne = std::get_if<BinaryExpr>(es2->expr);
        REQUIRE(bin_expr_ne != nullptr);
        CHECK_EQ(bin_expr_ne->op, BinOp::Ne);
    }

    SUBCASE("bitwise and") {
        static constexpr auto source = std::string_view("fn main() { a & b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::BitAnd);
    }

    SUBCASE("bitwise xor") {
        static constexpr auto source = std::string_view("fn main() { a ^ b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::BitXor);
    }

    SUBCASE("bitwise or") {
        static constexpr auto source = std::string_view("fn main() { a | b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::BitOr);
    }

    SUBCASE("logical and") {
        static constexpr auto source = std::string_view("fn main() { a && b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::LogicalAnd);
    }

    SUBCASE("logical or") {
        static constexpr auto source = std::string_view("fn main() { a || b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::LogicalOr);
    }

    SUBCASE("precedence: * before +") {
        static constexpr auto source = std::string_view("fn main() { a + b * c; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Add);
        // rhs should be b * c
        CHECK(std::get_if<BinaryExpr>(bin->rhs) != nullptr);
    }
}

// ============================================================
// Expressions — Assignment, Comma, Group
// ============================================================

TEST_CASE("Parser: assignment and compound assignment") {
    SUBCASE("simple assignment") {
        static constexpr auto source = std::string_view("fn main() { x = 5; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto assign = std::get_if<AssignExpr>(es->expr);
        CHECK(assign != nullptr);
    }

    SUBCASE("chained assignment") {
        static constexpr auto source = std::string_view("fn main() { a = b = c = 0; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto assign = std::get_if<AssignExpr>(es->expr);
        CHECK(assign != nullptr);
        // right-associative: a = (b = (c = 0))
        CHECK(std::get_if<AssignExpr>(assign->rhs) != nullptr);
    }

    SUBCASE("compound assignment +=") {
        static constexpr auto source = std::string_view("fn main() { x += 1; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ca = std::get_if<CompoundAssignExpr>(es->expr);
        CHECK(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Add);
    }

    SUBCASE("compound assignment -=") {
        static constexpr auto source = std::string_view("fn main() { x -= 1; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ca = std::get_if<CompoundAssignExpr>(es->expr);
        REQUIRE(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Sub);
    }

    SUBCASE("compound assignment *=") {
        static constexpr auto source = std::string_view("fn main() { x *= 2; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ca = std::get_if<CompoundAssignExpr>(es->expr);
        REQUIRE(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Mul);
    }
}

TEST_CASE("Parser: group and comma expressions") {
    SUBCASE("group expression") {
        static constexpr auto source = std::string_view("fn main() { (a + b); }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto group = std::get_if<GroupExpr>(es->expr);
        CHECK(group != nullptr);
    }

    SUBCASE("comma expression") {
        static constexpr auto source = std::string_view("fn main() { a, b; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto comma = std::get_if<CommaExpr>(es->expr);
        CHECK(comma != nullptr);
    }
}

// ============================================================
// Expressions — If Expression
// ============================================================

TEST_CASE("Parser: if as expression value") {
    SUBCASE("if expression assigned to let") {
        static constexpr auto source = std::string_view("fn main() { let x = if true { 1 } else { 2 }; }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK(decl->init != nullptr);
        CHECK(std::get_if<IfExpr>(decl->init) != nullptr);
    }
}

// ============================================================
// Statements — Block and Empty
// ============================================================

TEST_CASE("Parser: block and empty statements") {
    SUBCASE("empty statement") {
        static constexpr auto source = std::string_view("fn main() { ; }");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(std::get_if<EmptyStmt>(fn->body->statements[0]) != nullptr);
    }

    SUBCASE("multiple statements") {
        static constexpr auto source = std::string_view("fn main() { let a = 1; var b = 2; return a + b; }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK_EQ(fn->body->statements.size(), 3u);
    }

    SUBCASE("semicolon optional before brace") {
        static constexpr auto source = std::string_view("fn main() { call() }");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
}

// ============================================================
// Edge Cases
// ============================================================

TEST_CASE("Parser: edge cases") {
    SUBCASE("empty file") {
        static constexpr auto source = std::string_view("");
        const auto result = do_parse(source);
        CHECK(result.items.empty());
    }

    SUBCASE("only whitespace and comments") {
        static constexpr auto source = std::string_view("// comment only file\n// another");
        const auto result = do_parse(source);
        CHECK(result.items.empty());
    }

    SUBCASE("multiple top-level items") {
        static constexpr auto source = std::string_view(
            "import std;\n"
            "enum Color { Red, Green }\n"
            "struct Point { x: i32, y: i32 }\n"
            "fn main() { let p = Point { }; }"
        );
        const auto result = do_parse(source);
        CHECK_EQ(result.items.size(), 4u);
    }

    SUBCASE("unexpected keyword produces error") {
        static constexpr auto source = std::string_view("fn main() { let; }");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
    }
}

// ============================================================
// Error Recovery
// ============================================================

TEST_CASE("Parser: error recovery") {
    SUBCASE("skips to semicolon after bad statement") {
        static constexpr auto source = std::string_view("fn main() { var 123; x = 1; }");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        REQUIRE(fn != nullptr);
        // var 123 was consumed by error recovery; only x = 1 remains
        CHECK_EQ(fn->body->statements.size(), 1u);
    }

    SUBCASE("recovers from bad top-level item") {
        static constexpr auto source = std::string_view("garbage\nfn main() { }");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
        // Error consumed "garbage", fn should be first item
        CHECK(!result.items.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(fn != nullptr);
    }
}
