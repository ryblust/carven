#include "test_helpers.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import std;

#define PARSE(text) parse(tokenize(text), (text))

TEST_CASE("Parser: import") {
    SUBCASE("basic import") {
        const auto source = "import std;";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        CHECK_EQ(result.items.size(), 1u);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(slice(source, item->module_name), "std");
        CHECK(item->is_std_module);
    }

    SUBCASE("import without semicolon") {
        const auto source = "import foo";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        CHECK_EQ(result.items.size(), 1u);
    }

    SUBCASE("import with using declaration") {
        const auto source = "import std using println";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->using_decls.size(), 1u);
        CHECK_EQ(slice(source, item->using_decls[0]), "println");
    }

    SUBCASE("import with using wildcard") {
        const auto source = "import std using *";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->using_wildcard);
    }

    SUBCASE("import with using brace list") {
        const auto source = "import std using { println, format }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->using_decls.size(), 2u);
        CHECK_EQ(slice(source, item->using_decls[0]), "println");
        CHECK_EQ(slice(source, item->using_decls[1]), "format");
    }

    SUBCASE("import std auto-detection") {
        const auto source = "import std;";
        const auto result = PARSE(source);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->is_std_module);
    }

    SUBCASE("non-std module not auto-detected") {
        const auto source = "import other;";
        const auto result = PARSE(source);
        const auto item = std::get_if<ImportItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(!item->is_std_module);
    }
}

TEST_CASE("Parser: enum") {
    SUBCASE("basic enum") {
        const auto source = "enum Color { Red, Green, Blue }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(slice(source, item->name), "Color");
        CHECK_EQ(item->fields.size(), 3u);
        CHECK_EQ(slice(source, item->fields[0]), "Red");
        CHECK_EQ(slice(source, item->fields[1]), "Green");
        CHECK_EQ(slice(source, item->fields[2]), "Blue");
    }

    SUBCASE("enum with size type") {
        const auto source = "enum Color : u8 { Red, Green }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(slice(source, item->size), "u8");
        CHECK_EQ(item->fields.size(), 2u);
    }

    SUBCASE("single field enum") {
        const auto source = "enum Flag { Active }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 1u);
    }

    SUBCASE("empty enum") {
        const auto source = "enum Void {}";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<EnumItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK(item->fields.empty());
    }
}

TEST_CASE("Parser: struct") {
    SUBCASE("basic struct") {
        const auto source = "struct Point { x: i32, y: i32 }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(slice(source, item->name), "Point");
        CHECK_EQ(item->fields.size(), 2u);
        CHECK_EQ(slice(source, item->fields[0].name), "x");
        CHECK_EQ(slice(source, item->fields[0].type), "i32");
        CHECK_EQ(slice(source, item->fields[1].name), "y");
        CHECK_EQ(slice(source, item->fields[1].type), "i32");
    }

    SUBCASE("struct with semicolon separators") {
        const auto source = "struct Foo { a: i32; b: i32 }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 2u);
    }

    SUBCASE("single field struct") {
        const auto source = "struct Wrapper { value: i32 }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<StructItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->fields.size(), 1u);
    }
}

TEST_CASE("Parser: function") {
    SUBCASE("basic function") {
        const auto source = "fn main() { }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(slice(source, item->name), "main");
        CHECK(item->params.empty());
        CHECK(item->return_type.empty());
    }

    SUBCASE("function with return type") {
        const auto source = "fn add() -> i32 { }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(slice(source, item->return_type), "i32");
    }

    SUBCASE("function with typed parameters") {
        const auto source = "fn add(a: i32, b: i32) -> i32 { }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->params.size(), 2u);
        CHECK_EQ(slice(source, item->params[0].name), "a");
        CHECK_EQ(slice(source, item->params[0].type), "i32");
        CHECK_EQ(slice(source, item->params[1].name), "b");
        CHECK_EQ(slice(source, item->params[1].type), "i32");
    }

    SUBCASE("function with untyped parameters") {
        const auto source = "fn foo(x, y) { }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->params.size(), 2u);
        CHECK(item->params[0].type.empty());
        CHECK(item->params[1].type.empty());
    }

    SUBCASE("function with body statements") {
        const auto source = "fn main() { return 0; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto item = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(item != nullptr);
        CHECK_EQ(item->body->statements.size(), 1u);
    }
}

TEST_CASE("Parser: variable declarations") {
    SUBCASE("let with type and init") {
        const auto source = "fn main() { let x: i32 = 5; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(slice(source, decl->keyword), "let");
        CHECK_EQ(slice(source, decl->name), "x");
        CHECK_EQ(slice(source, decl->type), "i32");
        CHECK(!decl->eq.empty());
    }

    SUBCASE("var with init") {
        const auto source = "fn main() { var y = 10; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(slice(source, decl->keyword), "var");
        CHECK_EQ(slice(source, decl->name), "y");
    }

    SUBCASE("var without init") {
        const auto source = "fn main() { var y: i32; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(slice(source, decl->keyword), "var");
        CHECK_EQ(slice(source, decl->type), "i32");
        CHECK(decl->init == nullptr);
    }

    SUBCASE("const with init") {
        const auto source = "fn main() { const N = 42; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK_EQ(slice(source, decl->keyword), "const");
    }

    SUBCASE("let without init produces error") {
        const auto source = "fn main() { let x: i32; }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
    }

    SUBCASE("const without init produces error") {
        const auto source = "fn main() { const x: i32; }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
    }
}

TEST_CASE("Parser: return") {
    SUBCASE("return void") {
        const auto source = "fn main() { return; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ret = static_cast<const ReturnStmt*>(fn->body->statements[0]);
        CHECK(ret != nullptr);
        CHECK(ret->value == nullptr);
    }

    SUBCASE("return with value") {
        const auto source = "fn main() { return 42; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ret = static_cast<const ReturnStmt*>(fn->body->statements[0]);
        CHECK(ret != nullptr);
        CHECK(ret->value != nullptr);
    }
}

TEST_CASE("Parser: while") {
    SUBCASE("while with block body") {
        const auto source = "fn main() { while true { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const WhileStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->body->kind == StmtKind::Block);
    }

    SUBCASE("while with single statement body") {
        const auto source = "fn main() { while true return; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const WhileStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(static_cast<const ReturnStmt*>(loop->body) != nullptr);
    }

    SUBCASE("while with condition expression") {
        const auto source = "fn main() { while x > 0 { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const WhileStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(loop->condition != nullptr);
    }
}

TEST_CASE("Parser: for") {
    SUBCASE("full for loop") {
        const auto source = "fn main() { for (var i = 0; i < 10; i++) { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const ForStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::visit([](auto p) noexcept -> bool { return p != nullptr; }, loop->init));
        CHECK(loop->condition != nullptr);
        CHECK(loop->step != nullptr);
    }

    SUBCASE("for with empty clauses") {
        const auto source = "fn main() { for ( ; ; ) { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const ForStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(!std::visit([](auto p) noexcept -> bool { return p != nullptr; }, loop->init));
        CHECK(loop->condition == nullptr);
        CHECK(loop->step == nullptr);
    }

    SUBCASE("for with let init") {
        const auto source = "fn main() { for (let i = 0; i < 10; i++) { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const ForStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::visit([](auto p) noexcept -> bool { return p != nullptr; }, loop->init));
    }

    SUBCASE("for with expr init") {
        const auto source = "fn main() { for (i = 0; i < 10; i++) { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto loop = static_cast<const ForStmt*>(fn->body->statements[0]);
        CHECK(loop != nullptr);
        CHECK(std::visit([](auto p) noexcept -> bool { return p != nullptr; }, loop->init));
    }

    SUBCASE("for with const init") {
        const auto source = "fn main() { for (const i = 0; i < 5; i++) { } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: if expression") {
    SUBCASE("if without else") {
        const auto source = "fn main() { if true { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto expr_stmt = static_cast<const ExprStmt*>(fn->body->statements[0]);
        CHECK(expr_stmt != nullptr);
        const auto if_expr = static_cast<const IfExpr*>(expr_stmt->expr);
        CHECK(if_expr != nullptr);
        CHECK(if_expr->else_kw.empty());
    }

    SUBCASE("if with else") {
        const auto source = "fn main() { if true { } else { } }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto expr_stmt = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto if_expr = static_cast<const IfExpr*>(expr_stmt->expr);
        CHECK(if_expr != nullptr);
        CHECK(!if_expr->else_kw.empty());
        CHECK(if_expr->else_branch != nullptr);
    }
}

TEST_CASE("Parser: literal expressions") {
    SUBCASE("number, string, and char literals") {
        const auto source = "fn main() { 42; 3.14; \"hello\"; 'a'; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(static_cast<const LiteralExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr) != nullptr);
        CHECK(static_cast<const LiteralExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr) != nullptr);
        CHECK(static_cast<const LiteralExpr*>(static_cast<const ExprStmt*>(fn->body->statements[2])->expr) != nullptr);
        CHECK(static_cast<const LiteralExpr*>(static_cast<const ExprStmt*>(fn->body->statements[3])->expr) != nullptr);
    }

    SUBCASE("bool literals") {
        const auto source = "fn main() { true; false; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(static_cast<const LiteralExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr) != nullptr);
        CHECK(static_cast<const LiteralExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr) != nullptr);
    }
}

TEST_CASE("Parser: identifier and unary expressions") {
    SUBCASE("identifier") {
        const auto source = "fn main() { foo; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto ident = static_cast<const IdentExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr);
        CHECK(ident != nullptr);
        CHECK_EQ(slice(source, ident->name), "foo");
    }

    SUBCASE("prefix operators") {
        const auto source = "fn main() { -x; !flag; ~mask; ++i; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto p0 = static_cast<const PrefixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr);
        CHECK_EQ(p0->op, UnaryOp::Neg);
        const auto p1 = static_cast<const PrefixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr);
        CHECK_EQ(p1->op, UnaryOp::LogicalNot);
        const auto p2 = static_cast<const PrefixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[2])->expr);
        CHECK_EQ(p2->op, UnaryOp::BitNot);
        const auto p3 = static_cast<const PrefixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[3])->expr);
        CHECK_EQ(p3->op, UnaryOp::PreInc);
    }

    SUBCASE("prefix address-of and deref") {
        const auto source = "fn main() { &x; *p; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto p0 = static_cast<const PrefixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr);
        CHECK_EQ(p0->op, UnaryOp::AddressOf);
        const auto p1 = static_cast<const PrefixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr);
        CHECK_EQ(p1->op, UnaryOp::Deref);
    }

    SUBCASE("postfix operators") {
        const auto source = "fn main() { i++; j--; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto p0 = static_cast<const PostfixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr);
        CHECK_EQ(p0->op, UnaryOp::PostInc);
        const auto p1 = static_cast<const PostfixExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr);
        CHECK_EQ(p1->op, UnaryOp::PostDec);
    }
}

TEST_CASE("Parser: postfix operations") {
    SUBCASE("function calls with varying args") {
        const auto source = "fn main() { foo(); bar(1); add(1, 2, 3); }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto c0 = static_cast<const CallExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr);
        CHECK(c0->args.empty());
        const auto c1 = static_cast<const CallExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr);
        CHECK_EQ(c1->args.size(), 1u);
        const auto c2 = static_cast<const CallExpr*>(static_cast<const ExprStmt*>(fn->body->statements[2])->expr);
        CHECK_EQ(c2->args.size(), 3u);
    }

    SUBCASE("index expression") {
        const auto source = "fn main() { arr[0]; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(static_cast<const IndexExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr) != nullptr);
    }

    SUBCASE("field access: dot, arrow, scope") {
        const auto source = "fn main() { obj.field; ptr->field; ns::name; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto f0 = static_cast<const FieldExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr);
        CHECK_EQ(slice(source, f0->dot), ".");
        const auto f1 = static_cast<const FieldExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr);
        CHECK_EQ(slice(source, f1->dot), "->");
        const auto f2 = static_cast<const FieldExpr*>(static_cast<const ExprStmt*>(fn->body->statements[2])->expr);
        CHECK_EQ(slice(source, f2->dot), "::");
    }
}

TEST_CASE("Parser: binary expressions") {
    SUBCASE("multiplicative: multiply, divide, modulo") {
        const auto source = "fn main() { a * b; c / d; e % f; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr)->op, BinOp::Mul);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr)->op, BinOp::Div);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[2])->expr)->op, BinOp::Mod);
    }

    SUBCASE("additive: plus and minus") {
        const auto source = "fn main() { a + b - c; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto bin = static_cast<const BinaryExpr*>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Sub);
    }

    SUBCASE("shift: left and right") {
        const auto source = "fn main() { a << b >> c; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto bin = static_cast<const BinaryExpr*>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Shr);
    }

    SUBCASE("relational: less, greater, le, ge") {
        const auto source = "fn main() { a < b; b > c; c <= d; d >= e; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es1 = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto es2 = static_cast<const ExprStmt*>(fn->body->statements[1]);
        const auto es3 = static_cast<const ExprStmt*>(fn->body->statements[2]);
        const auto es4 = static_cast<const ExprStmt*>(fn->body->statements[3]);
        const auto bin_expr_lt = static_cast<const BinaryExpr*>(es1->expr);
        const auto bin_expr_gt = static_cast<const BinaryExpr*>(es2->expr);
        const auto bin_expr_le = static_cast<const BinaryExpr*>(es3->expr);
        const auto bin_expr_ge = static_cast<const BinaryExpr*>(es4->expr);
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
        const auto source = "fn main() { a == b; c != d; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        // a == b
        const auto es1 = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto bin_expr_eq = static_cast<const BinaryExpr*>(es1->expr);
        REQUIRE(bin_expr_eq != nullptr);
        CHECK_EQ(bin_expr_eq->op, BinOp::Eq);
        // c != d
        const auto es2 = static_cast<const ExprStmt*>(fn->body->statements[1]);
        const auto bin_expr_ne = static_cast<const BinaryExpr*>(es2->expr);
        REQUIRE(bin_expr_ne != nullptr);
        CHECK_EQ(bin_expr_ne->op, BinOp::Ne);
    }

    SUBCASE("bitwise: and, xor, or") {
        const auto source = "fn main() { a & b; c ^ d; e | f; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr)->op, BinOp::BitAnd);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr)->op, BinOp::BitXor);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[2])->expr)->op, BinOp::BitOr);
    }

    SUBCASE("logical: and, or") {
        const auto source = "fn main() { a && b; c || d; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[0])->expr)->op, BinOp::LogicalAnd);
        CHECK_EQ(static_cast<const BinaryExpr*>(static_cast<const ExprStmt*>(fn->body->statements[1])->expr)->op, BinOp::LogicalOr);
    }

    SUBCASE("precedence: * before +") {
        const auto source = "fn main() { a + b * c; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto bin = static_cast<const BinaryExpr*>(es->expr);
        CHECK(bin != nullptr);
        CHECK_EQ(bin->op, BinOp::Add);
        // rhs should be b * c
        CHECK(static_cast<const BinaryExpr*>(bin->rhs) != nullptr);
    }
}

TEST_CASE("Parser: assignment and compound assignment") {
    SUBCASE("simple assignment") {
        const auto source = "fn main() { x = 5; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto assign = static_cast<const AssignExpr*>(es->expr);
        CHECK(assign != nullptr);
    }

    SUBCASE("chained assignment") {
        const auto source = "fn main() { a = b = c = 0; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto assign = static_cast<const AssignExpr*>(es->expr);
        CHECK(assign != nullptr);
        // right-associative: a = (b = (c = 0))
        CHECK(static_cast<const AssignExpr*>(assign->rhs) != nullptr);
    }

    SUBCASE("compound assignment +=") {
        const auto source = "fn main() { x += 1; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto ca = static_cast<const CompoundAssignExpr*>(es->expr);
        CHECK(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Add);
    }

    SUBCASE("compound assignment -=") {
        const auto source = "fn main() { x -= 1; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto ca = static_cast<const CompoundAssignExpr*>(es->expr);
        REQUIRE(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Sub);
    }

    SUBCASE("compound assignment *=") {
        const auto source = "fn main() { x *= 2; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto ca = static_cast<const CompoundAssignExpr*>(es->expr);
        REQUIRE(ca != nullptr);
        CHECK_EQ(ca->op, BinOp::Mul);
    }
}

TEST_CASE("Parser: group and comma expressions") {
    SUBCASE("group expression") {
        const auto source = "fn main() { (a + b); }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto group = static_cast<const GroupExpr*>(es->expr);
        CHECK(group != nullptr);
    }

    SUBCASE("comma expression") {
        const auto source = "fn main() { a, b; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        const auto comma = static_cast<const CommaExpr*>(es->expr);
        CHECK(comma != nullptr);
    }
}

TEST_CASE("Parser: if as expression value") {
    SUBCASE("if expression assigned to let") {
        const auto source = "fn main() { let x = if true { 1 } else { 2 }; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        CHECK(decl != nullptr);
        CHECK(decl->init != nullptr);
        CHECK(static_cast<const IfExpr*>(decl->init) != nullptr);
    }
}

TEST_CASE("Parser: block and empty statements") {
    SUBCASE("empty statement") {
        const auto source = "fn main() { ; }";
        const auto result = PARSE(source);
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(static_cast<const EmptyStmt*>(fn->body->statements[0]) != nullptr);
    }

    SUBCASE("multiple statements") {
        const auto source = "fn main() { let a = 1; var b = 2; return a + b; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK_EQ(fn->body->statements.size(), 3u);
    }

    SUBCASE("semicolon optional before brace") {
        const auto source = "fn main() { call() }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: edge cases") {
    SUBCASE("empty file or comments only") {
        CHECK(PARSE("").items.empty());
        CHECK(PARSE("// comment only").items.empty());
    }

    SUBCASE("multiple top-level items") {
        const auto source =
            "import std;\n"
            "enum Color { Red, Green }\n"
            "struct Point { x: i32, y: i32 }\n"
            "fn main() { let p = Point { }; }";
        const auto result = PARSE(source);
        CHECK_EQ(result.items.size(), 4u);
    }

    SUBCASE("unexpected keyword produces error") {
        const auto source = "fn main() { let; }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
    }
}

TEST_CASE("Parser: error recovery") {
    SUBCASE("skips to semicolon after bad statement") {
        const auto source = "fn main() { var 123; x = 1; }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        REQUIRE(fn != nullptr);
        // var 123 was consumed by error recovery; only x = 1 remains
        CHECK_EQ(fn->body->statements.size(), 1u);
    }

    SUBCASE("recovers from bad top-level item") {
        const auto source = "garbage\nfn main() { }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
        // Error consumed "garbage", fn should be first item
        CHECK(!result.items.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        CHECK(fn != nullptr);
    }
}
TEST_CASE("Parser: trailing commas") {
    SUBCASE("struct field trailing comma") {
        const auto source = "struct Foo { x: i32, }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("enum field trailing comma") {
        const auto source = "enum Bar { A, }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: deeply nested expressions") {
    const auto source = "fn main() { (((a + b) * (c + d)) + e); }";
    const auto result = PARSE(source);
    CHECK(result.errors.empty());
}

TEST_CASE("Parser: chained postfix") {
    const auto source = "fn main() { a[0](1).b(); }";
    const auto result = PARSE(source);
    CHECK(result.errors.empty());
}

TEST_CASE("Parser: EOF mid-parse") {
    const auto source = "fn main() { let x = ";
    const auto result = PARSE(source);
    CHECK(!result.errors.empty());
}

TEST_CASE("Parser: array literals") {
    SUBCASE("simple array literal") {
        const auto source = "fn main() { let a = [1, 2, 3]; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        REQUIRE(fn != nullptr);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        REQUIRE(decl != nullptr);
        const auto arr = static_cast<const ArrayExpr*>(decl->init);
        REQUIRE(arr != nullptr);
        CHECK_EQ(arr->elements.size(), 3u);
    }
    SUBCASE("single element array") {
        const auto source = "fn main() { let a = [42]; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("trailing comma") {
        const auto source = "fn main() { let a = [1, 2, 3, ]; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("nested array") {
        const auto source = "fn main() { let m = [[1, 2], [3, 4]]; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("array as function argument") {
        const auto source = "fn main() { f([1, 2]); }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: array type annotation") {
    SUBCASE("let with array type") {
        const auto source = "fn main() { let a: [i32; 3] = [1, 2, 3]; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        REQUIRE(fn != nullptr);
        const auto decl = static_cast<const VarDecl*>(fn->body->statements[0]);
        REQUIRE(decl != nullptr);
        CHECK(slice(source, decl->type).starts_with("["));
        CHECK(slice(source, decl->type).ends_with("]"));
    }
    SUBCASE("function parameter array type") {
        const auto source = "fn f(p: [i32; 3]) { }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("function return array type") {
        const auto source = "fn f() -> [i32; 3] { }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("struct field array type") {
        const auto source = "struct S { f: [f64; 4] }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("var with array type") {
        const auto source = "fn main() { var a: [u8; 16] = [0, 0]; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: match expression") {
    SUBCASE("value match") {
        const auto source = "fn main() { match x { 1 => { a } _ => { b } } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
        const auto fn = std::get_if<FunctionItem>(&result.items[0]);
        REQUIRE(fn != nullptr);
        const auto es = static_cast<const ExprStmt*>(fn->body->statements[0]);
        REQUIRE(es != nullptr);
        const auto m = static_cast<const MatchExpr*>(es->expr);
        REQUIRE(m != nullptr);
        CHECK_EQ(m->arms.size(), 2u);
        CHECK(m->arms[1].is_wildcard);
        CHECK_EQ(m->arms[0].patterns.size(), 1u);
    }
    SUBCASE("type match") {
        const auto source = "fn main() { match x { i32 => { a } String => { b } _ => { c } } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("multi-pattern value") {
        const auto source = "fn main() { match x { 1 | 2 => { a } _ => { b } } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("multi-pattern type") {
        const auto source = "fn main() { match x { i32 | f64 => { a } _ => { b } } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("mixed value and type") {
        const auto source = "fn main() { match x { 0 => { a } i32 => { b } _ => { c } } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("match as expression") {
        const auto source = "fn main() { let a = match x { 1 => { 10 } _ => { 0 } }; }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
    SUBCASE("nested match") {
        const auto source = "fn main() { match a { 1 => { match b { i32 => { x } _ => { y } } } _ => { z } } }";
        const auto result = PARSE(source);
        CHECK(result.errors.empty());
    }
}

TEST_CASE("Parser: match errors") {
    SUBCASE("match without braces") {
        const auto source = "fn main() { match x 1 => { } }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
    }
    SUBCASE("arm without fat arrow") {
        const auto source = "fn main() { match x { 1 { } } }";
        const auto result = PARSE(source);
        CHECK(!result.errors.empty());
    }
}
