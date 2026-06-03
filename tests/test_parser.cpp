#include "doctest.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

static constexpr auto do_parse(const SourceFile& file) noexcept -> ParseResult {
    return parse(tokenize(file.text()), file);
}

TEST_CASE("Parser: import") {
    SUBCASE("basic import") {
        const auto source = SourceFile("import std;", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        CHECK_EQ(result.items.size(), 1u);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(source.slice(item->module_name), "std");
        CHECK(item->is_std_module);
    }

    SUBCASE("import without semicolon") {
        const auto source = SourceFile("import foo", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        CHECK_EQ(result.items.size(), 1u);
    }

    SUBCASE("import with using declaration") {
        const auto source = SourceFile("import std using println", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->using_decls.size(), 1u);
        CHECK_EQ(source.slice(item->using_decls[0]), "println");
    }

    SUBCASE("import with using wildcard") {
        const auto source = SourceFile("import std using *", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->using_wildcard);
    }

    SUBCASE("import with using brace list") {
        const auto source = SourceFile("import std using { println, format }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->using_decls.size(), 2u);
        CHECK_EQ(source.slice(item->using_decls[0]), "println");
        CHECK_EQ(source.slice(item->using_decls[1]), "format");
    }

    SUBCASE("import std auto-detection") {
        const auto source = SourceFile("import std;", "<test>");
        const auto result = do_parse(source);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->is_std_module);
    }

    SUBCASE("non-std module not auto-detected") {
        const auto source = SourceFile("import other;", "<test>");
        const auto result = do_parse(source);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(!item->is_std_module);
    }
}

TEST_CASE("Parser: enum") {
    SUBCASE("basic enum") {
        const auto source = SourceFile("enum Color { Red, Green, Blue }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(source.slice(item->name), "Color");
        CHECK_EQ(item->fields.size(), 3u);
        CHECK_EQ(source.slice(item->fields[0]), "Red");
        CHECK_EQ(source.slice(item->fields[1]), "Green");
        CHECK_EQ(source.slice(item->fields[2]), "Blue");
    }

    SUBCASE("enum with size type") {
        const auto source = SourceFile("enum Color : u8 { Red, Green }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(source.slice(item->size), "u8");
        CHECK_EQ(item->fields.size(), 2u);
    }

    SUBCASE("single field enum") {
        const auto source = SourceFile("enum Flag { Active }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 1u);
    }

    SUBCASE("empty enum") {
        const auto source = SourceFile("enum Void {}", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->fields.empty());
    }
}

TEST_CASE("Parser: struct") {
    SUBCASE("basic struct") {
        const auto source = SourceFile("struct Point { x: i32, y: i32 }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(source.slice(item->name), "Point");
        CHECK_EQ(item->fields.size(), 2u);
        CHECK_EQ(source.slice(item->fields[0].name), "x");
        CHECK_EQ(source.slice(item->fields[0].type), "i32");
        CHECK_EQ(source.slice(item->fields[1].name), "y");
        CHECK_EQ(source.slice(item->fields[1].type), "i32");
    }

    SUBCASE("struct with semicolon separators") {
        const auto source = SourceFile("struct Foo { a: i32; b: i32 }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 2u);
    }

    SUBCASE("single field struct") {
        const auto source = SourceFile("struct Wrapper { value: i32 }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 1u);
    }
}

TEST_CASE("Parser: function") {
    SUBCASE("basic function") {
        const auto source = SourceFile("fn main() { }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(source.slice(item->name), "main");
        CHECK(item->params.empty());
        CHECK(item->return_type.empty());
    }

    SUBCASE("function with return type") {
        const auto source = SourceFile("fn add() -> i32 { }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(source.slice(item->return_type), "i32");
    }

    SUBCASE("function with typed parameters") {
        const auto source = SourceFile("fn add(a: i32, b: i32) -> i32 { }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->params.size(), 2u);
        CHECK_EQ(source.slice(item->params[0].name), "a");
        CHECK_EQ(source.slice(item->params[0].type), "i32");
        CHECK_EQ(source.slice(item->params[1].name), "b");
        CHECK_EQ(source.slice(item->params[1].type), "i32");
    }

    SUBCASE("function with untyped parameters") {
        const auto source = SourceFile("fn foo(x, y) { }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->params.size(), 2u);
        CHECK(item->params[0].type.empty());
        CHECK(item->params[1].type.empty());
    }

    SUBCASE("function with body statements") {
        const auto source = SourceFile("fn main() { return 0; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->body->statements.size(), 1u);
    }
}

TEST_CASE("Parser: variable declarations") {
    SUBCASE("let with type and init") {
        const auto source = SourceFile("fn main() { let x: i32 = 5; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(source.slice(decl->keyword), "let");
        CHECK_EQ(source.slice(decl->name), "x");
        CHECK_EQ(source.slice(decl->type), "i32");
        CHECK(!decl->eq.empty());
    }

    SUBCASE("var with init") {
        const auto source = SourceFile("fn main() { var y = 10; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(source.slice(decl->keyword), "var");
        CHECK_EQ(source.slice(decl->name), "y");
    }

    SUBCASE("var without init") {
        const auto source = SourceFile("fn main() { var y: i32; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(source.slice(decl->keyword), "var");
        CHECK_EQ(source.slice(decl->type), "i32");
        CHECK(decl->init == nullptr);
    }

    SUBCASE("const with init") {
        const auto source = SourceFile("fn main() { const N = 42; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(source.slice(decl->keyword), "const");
    }

    SUBCASE("let without init produces error") {
        const auto source = SourceFile("fn main() { let x: i32; }", "<test>");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
    }

    SUBCASE("const without init produces error") {
        const auto source = SourceFile("fn main() { const x: i32; }", "<test>");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
    }
}

TEST_CASE("Parser: return") {
    SUBCASE("return void") {
        const auto source = SourceFile("fn main() { return; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ret = std::get_if<ReturnStmt>(fn->body->statements[0]);
        CHECK(ret != nullptr);
        CHECK(ret->value == nullptr);
    }

    SUBCASE("return with value") {
        const auto source = SourceFile("fn main() { return 42; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ret = std::get_if<ReturnStmt>(fn->body->statements[0]);
        CHECK(ret != nullptr);
        CHECK(ret->value != nullptr);
    }
}

TEST_CASE("Parser: while") {
    SUBCASE("while with block body") {
        const auto source = SourceFile("fn main() { while true { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<WhileStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::get_if<BlockStmt>(loop->body) != nullptr);
    }

    SUBCASE("while with single statement body") {
        const auto source = SourceFile("fn main() { while true return; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<WhileStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::get_if<ReturnStmt>(loop->body) != nullptr);
    }

    SUBCASE("while with condition expression") {
        const auto source = SourceFile("fn main() { while x > 0 { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<WhileStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->condition != nullptr);
    }
}

TEST_CASE("Parser: for") {
    SUBCASE("full for loop") {
        const auto source = SourceFile("fn main() { for (var i = 0; i < 10; i++) { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init != nullptr);
        CHECK(loop->condition != nullptr);
        CHECK(loop->step != nullptr);
    }

    SUBCASE("for with empty clauses") {
        const auto source = SourceFile("fn main() { for ( ; ; ) { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init == nullptr);
        CHECK(loop->condition == nullptr);
        CHECK(loop->step == nullptr);
    }

    SUBCASE("for with let init") {
        const auto source = SourceFile("fn main() { for (let i = 0; i < 10; i++) { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init != nullptr);
    }

    SUBCASE("for with expr init") {
        const auto source = SourceFile("fn main() { for (i = 0; i < 10; i++) { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = std::get_if<ForStmt>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->init != nullptr);
    }
}

TEST_CASE("Parser: if expression") {
    SUBCASE("if without else") {
        const auto source = SourceFile("fn main() { if true { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto expr_stmt = std::get_if<ExprStmt>(fn->body->statements[0]);
        CHECK(expr_stmt != nullptr);
        const auto if_expr = std::get_if<IfExpr>(expr_stmt->expr);
        CHECK(if_expr != nullptr);
        CHECK(!if_expr->else_kw.has_value());
    }

    SUBCASE("if with else") {
        const auto source = SourceFile("fn main() { if true { } else { } }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto expr_stmt = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto if_expr = std::get_if<IfExpr>(expr_stmt->expr);
        CHECK(if_expr != nullptr);
        CHECK(if_expr->else_kw.has_value());
        CHECK(if_expr->else_branch.has_value());
    }
}

TEST_CASE("Parser: literal expressions") {
    SUBCASE("integer literal") {
        const auto source = SourceFile("fn main() { 42; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(source.slice(lit->token), "42");
    }

    SUBCASE("float literal") {
        const auto source = SourceFile("fn main() { 3.14; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(source.slice(lit->token), "3.14");
    }

    SUBCASE("string literal") {
        const auto source = SourceFile("fn main() { \"hello\"; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(source.slice(lit->token), "\"hello\"");
    }

    SUBCASE("char literal") {
        const auto source = SourceFile("fn main() { 'a'; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto lit = std::get_if<LiteralExpr>(es->expr);
        CHECK(lit != nullptr);
        CHECK_EQ(source.slice(lit->token), "'a'");
    }

    SUBCASE("bool literals") {
        const auto source = SourceFile("fn main() { true; false; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es1 = std::get_if<ExprStmt>(fn->body->statements[0]);
        CHECK(std::get_if<LiteralExpr>(es1->expr) != nullptr);
        const auto es2 = std::get_if<ExprStmt>(fn->body->statements[1]);
        CHECK(std::get_if<LiteralExpr>(es2->expr) != nullptr);
    }
}

TEST_CASE("Parser: identifier and unary expressions") {
    SUBCASE("identifier") {
        const auto source = SourceFile("fn main() { foo; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ident = std::get_if<IdentExpr>(es->expr);
        CHECK(ident != nullptr);
        CHECK_EQ(source.slice(ident->name), "foo");
    }

    SUBCASE("prefix negation") {
        const auto source = SourceFile("fn main() { -x; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::Neg);
    }

    SUBCASE("prefix logical not") {
        const auto source = SourceFile("fn main() { !flag; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::LogicalNot);
    }

    SUBCASE("prefix bitwise not") {
        const auto source = SourceFile("fn main() { ~mask; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::BitNot);
    }

    SUBCASE("prefix pre-increment") {
        const auto source = SourceFile("fn main() { ++i; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::PreInc);
    }

    SUBCASE("prefix pre-decrement") {
        const auto source = SourceFile("fn main() { --i; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::PreDec);
    }

    SUBCASE("prefix address-of") {
        const auto source = SourceFile("fn main() { &x; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::AddressOf);
    }

    SUBCASE("prefix dereference") {
        const auto source = SourceFile("fn main() { *p; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto pre = std::get_if<PrefixExpr>(es->expr);
        CHECK(pre != nullptr);
        CHECK_EQ(pre->op, UnaryOp::Deref);
    }

    SUBCASE("postfix increment") {
        const auto source = SourceFile("fn main() { i++; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto post = std::get_if<PostfixExpr>(es->expr);
        CHECK(post != nullptr);
        CHECK_EQ(post->op, UnaryOp::PostInc);
    }

    SUBCASE("postfix decrement") {
        const auto source = SourceFile("fn main() { i--; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto post = std::get_if<PostfixExpr>(es->expr);
        CHECK(post != nullptr);
        CHECK_EQ(post->op, UnaryOp::PostDec);
    }
}

TEST_CASE("Parser: postfix operations") {
    SUBCASE("function call with no args") {
        const auto source = SourceFile("fn main() { foo(); }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto call = std::get_if<CallExpr>(es->expr);
        CHECK(call != nullptr);
        CHECK(call->args.empty());
    }

    SUBCASE("function call with one arg") {
        const auto source = SourceFile("fn main() { foo(1); }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto call = std::get_if<CallExpr>(es->expr);
        CHECK(call != nullptr);
        CHECK_EQ(call->args.size(), 1u);
    }

    SUBCASE("function call with multiple args") {
        const auto source = SourceFile("fn main() { add(1, 2, 3); }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto call = std::get_if<CallExpr>(es->expr);
        CHECK(call != nullptr);
        CHECK_EQ(call->args.size(), 3u);
    }

    SUBCASE("index expression") {
        const auto source = SourceFile("fn main() { arr[0]; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto idx = std::get_if<IndexExpr>(es->expr);
        CHECK(idx != nullptr);
    }

    SUBCASE("dot field access") {
        const auto source = SourceFile("fn main() { obj.field; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto field = std::get_if<FieldExpr>(es->expr);
        CHECK(field != nullptr);
        CHECK_EQ(source.slice(field->dot), ".");
        CHECK_EQ(source.slice(field->field), "field");
    }

    SUBCASE("arrow field access") {
        const auto source = SourceFile("fn main() { ptr->field; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto field = std::get_if<FieldExpr>(es->expr);
        CHECK(field != nullptr);
        CHECK_EQ(source.slice(field->dot), "->");
    }

    SUBCASE("scope access") {
        const auto source = SourceFile("fn main() { ns::name; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto field = std::get_if<FieldExpr>(es->expr);
        CHECK(field != nullptr);
        CHECK_EQ(source.slice(field->dot), "::");
    }
}

TEST_CASE("Parser: binary expressions") {
    SUBCASE("multiplicative: multiply") {
        const auto source = SourceFile("fn main() { a * b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Mul);
    }

    SUBCASE("multiplicative: divide") {
        const auto source = SourceFile("fn main() { a / b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Div);
    }

    SUBCASE("multiplicative: modulo") {
        const auto source = SourceFile("fn main() { a % b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Mod);
    }

    SUBCASE("additive: plus and minus") {
        const auto source = SourceFile("fn main() { a + b - c; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Sub);
    }

    SUBCASE("shift: left and right") {
        const auto source = SourceFile("fn main() { a << b >> c; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Shr);
    }

    SUBCASE("relational: less, greater, le, ge") {
        const auto source = SourceFile("fn main() { a < b; b > c; c <= d; d >= e; }", "<test>");
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
        const auto source = SourceFile("fn main() { a == b; c != d; }", "<test>");
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
        const auto source = SourceFile("fn main() { a & b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::BitAnd);
    }

    SUBCASE("bitwise xor") {
        const auto source = SourceFile("fn main() { a ^ b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::BitXor);
    }

    SUBCASE("bitwise or") {
        const auto source = SourceFile("fn main() { a | b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::BitOr);
    }

    SUBCASE("logical and") {
        const auto source = SourceFile("fn main() { a && b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::LogicalAnd);
    }

    SUBCASE("logical or") {
        const auto source = SourceFile("fn main() { a || b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto bin = std::get_if<BinaryExpr>(es->expr);
        REQUIRE(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::LogicalOr);
    }

    SUBCASE("precedence: * before +") {
        const auto source = SourceFile("fn main() { a + b * c; }", "<test>");
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

TEST_CASE("Parser: assignment and compound assignment") {
    SUBCASE("simple assignment") {
        const auto source = SourceFile("fn main() { x = 5; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto assign = std::get_if<AssignExpr>(es->expr);
        CHECK(assign != nullptr);
    }

    SUBCASE("chained assignment") {
        const auto source = SourceFile("fn main() { a = b = c = 0; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto assign = std::get_if<AssignExpr>(es->expr);
        CHECK(assign != nullptr);
        // right-associative: a = (b = (c = 0))
        CHECK(std::get_if<AssignExpr>(assign->rhs) != nullptr);
    }

    SUBCASE("compound assignment +=") {
        const auto source = SourceFile("fn main() { x += 1; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ca = std::get_if<CompoundAssignExpr>(es->expr);
        CHECK(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Add);
    }

    SUBCASE("compound assignment -=") {
        const auto source = SourceFile("fn main() { x -= 1; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto ca = std::get_if<CompoundAssignExpr>(es->expr);
        REQUIRE(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Sub);
    }

    SUBCASE("compound assignment *=") {
        const auto source = SourceFile("fn main() { x *= 2; }", "<test>");
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
        const auto source = SourceFile("fn main() { (a + b); }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto group = std::get_if<GroupExpr>(es->expr);
        CHECK(group != nullptr);
    }

    SUBCASE("comma expression") {
        const auto source = SourceFile("fn main() { a, b; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = std::get_if<ExprStmt>(fn->body->statements[0]);
        const auto comma = std::get_if<CommaExpr>(es->expr);
        CHECK(comma != nullptr);
    }
}

TEST_CASE("Parser: if as expression value") {
    SUBCASE("if expression assigned to let") {
        const auto source = SourceFile("fn main() { let x = if true { 1 } else { 2 }; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = std::get_if<VarDecl>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK(decl->init != nullptr);
        CHECK(std::get_if<IfExpr>(decl->init) != nullptr);
    }
}

TEST_CASE("Parser: block and empty statements") {
    SUBCASE("empty statement") {
        const auto source = SourceFile("fn main() { ; }", "<test>");
        const auto result = do_parse(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(std::get_if<EmptyStmt>(fn->body->statements[0]) != nullptr);
    }

    SUBCASE("multiple statements") {
        const auto source = SourceFile("fn main() { let a = 1; var b = 2; return a + b; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK_EQ(fn->body->statements.size(), 3u);
    }

    SUBCASE("semicolon optional before brace") {
        const auto source = SourceFile("fn main() { call() }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: edge cases") {
    SUBCASE("empty file") {
        const auto source = SourceFile("", "<test>");
        const auto result = do_parse(source);
        CHECK(result.items.empty());
    }

    SUBCASE("only whitespace and comments") {
        const auto source = SourceFile("// comment only file\n// another", "<test>");
        const auto result = do_parse(source);
        CHECK(result.items.empty());
    }

    SUBCASE("multiple top-level items") {
        const auto source = SourceFile(
            "import std;\n"
            "enum Color { Red, Green }\n"
            "struct Point { x: i32, y: i32 }\n"
            "fn main() { let p = Point { }; }"
        );
        const auto result = do_parse(source);
        CHECK_EQ(result.items.size(), 4u);
    }

    SUBCASE("unexpected keyword produces error") {
        const auto source = SourceFile("fn main() { let; }", "<test>");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
    }
}

TEST_CASE("Parser: error recovery") {
    SUBCASE("skips to semicolon after bad statement") {
        const auto source = SourceFile("fn main() { var 123; x = 1; }", "<test>");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        REQUIRE(fn != nullptr);
        // var 123 was consumed by error recovery; only x = 1 remains
        CHECK_EQ(fn->body->statements.size(), 1u);
    }

    SUBCASE("recovers from bad top-level item") {
        const auto source = SourceFile("garbage\nfn main() { }", "<test>");
        const auto result = do_parse(source);
        CHECK(!result.errors.empty());
        // Error consumed "garbage", fn should be first item
        CHECK(!result.items.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(fn != nullptr);
    }
}

TEST_CASE("Parser: all compound assignment operators") {
    SUBCASE("/= compound") {
        const auto source = SourceFile("fn main() { x /= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("%= compound") {
        const auto source = SourceFile("fn main() { x %= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("&= compound") {
        const auto source = SourceFile("fn main() { x &= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("|= compound") {
        const auto source = SourceFile("fn main() { x |= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("^= compound") {
        const auto source = SourceFile("fn main() { x ^= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("<<= compound") {
        const auto source = SourceFile("fn main() { x <<= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE(">>= compound") {
        const auto source = SourceFile("fn main() { x >>= 2; }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: const in for init") {
    const auto source = SourceFile("fn main() { for (const i = 0; i < 5; i++) { } }", "<test>");
    const auto result = do_parse(source);
    CHECK(result.errors.empty());
}

TEST_CASE("Parser: trailing commas") {
    SUBCASE("struct field trailing comma") {
        const auto source = SourceFile("struct Foo { x: i32, }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("enum field trailing comma") {
        const auto source = SourceFile("enum Bar { A, }", "<test>");
        const auto result = do_parse(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: deeply nested expressions") {
    const auto source = SourceFile("fn main() { (((a + b) * (c + d)) + e); }", "<test>");
    const auto result = do_parse(source);
    CHECK(result.errors.empty());
}

TEST_CASE("Parser: chained postfix") {
    const auto source = SourceFile("fn main() { a[0](1).b(); }", "<test>");
    const auto result = do_parse(source);
    CHECK(result.errors.empty());
}

TEST_CASE("Parser: EOF mid-parse") {
    const auto source = SourceFile("fn main() { let x = ", "<test>");
    const auto result = do_parse(source);
    CHECK(!result.errors.empty());
}
