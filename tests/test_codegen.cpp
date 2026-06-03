#include "doctest.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import carven.backend.codegen;
import std;

// Helper: parse and generate C++ — default to C++23, no std preamble
static auto gen(std::string_view source_text, bool import_std = false, std::uint8_t standard = 23) noexcept -> std::string {
    const auto sf = SourceFile(source_text, "<test>");
    const auto result = parse(tokenize(sf.text()), sf);
    return generate(result.items, sf, standard, import_std);
}

TEST_CASE("Codegen: type mapping") {
    SUBCASE("var with i32 type") {
        const auto cpp = gen("fn main() { var x: i32 = 42; }");
        CHECK(cpp.contains("std::int32_t(42)"));
    }

    SUBCASE("var with f64 type") {
        const auto cpp = gen("fn main() { var x: f64 = 3.14; }");
        CHECK(cpp.contains("double(3.14)"));
    }

    SUBCASE("var with u8 type") {
        const auto cpp = gen("fn main() { var x: u8 = 255; }");
        CHECK(cpp.contains("std::uint8_t(255)"));
    }

    SUBCASE("let with i64 type") {
        const auto cpp = gen("fn main() { let x: i64 = 100; }");
        CHECK(cpp.contains("const auto x = std::int64_t(100)"));
    }

    SUBCASE("var with usize type") {
        const auto cpp = gen("fn main() { var x: usize = 0; }");
        CHECK(cpp.contains("std::size_t(0)"));
    }
}

TEST_CASE("Codegen: include preamble") {
    SUBCASE("disabled when import_std is false") {
        const auto cpp = gen("fn main() { }", false);
        CHECK(!cpp.contains("#include"));
    }

    SUBCASE("c++20 preamble") {
        const auto cpp = gen("fn main() { }", true, 20);
        CHECK(cpp.contains("#include <compare>"));
        CHECK(cpp.contains("#include <span>"));
    }

    SUBCASE("c++17 preamble") {
        const auto cpp = gen("fn main() { }", true, 17);
        CHECK(cpp.contains("#include <optional>"));
        CHECK(cpp.contains("#include <variant>"));
    }

    SUBCASE("c++23 preamble") {
        const auto cpp = gen("fn main() { }", true, 23);
        CHECK(cpp.contains("#include <expected>"));
        CHECK(cpp.contains("#include <print>"));
    }

    SUBCASE("c++14 baseline preamble") {
        const auto cpp = gen("fn main() { }", true, 14);
        CHECK(cpp.contains("#include <vector>"));
    }

    SUBCASE("c++26 falls back to highest available") {
        const auto cpp = gen("fn main() { }", true, 26);
        CHECK(cpp.contains("#include"));
    }
}

TEST_CASE("Codegen: import items") {
    SUBCASE("non-std module") {
        const auto source = SourceFile("import mylib;", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("import mylib;"));
    }

    SUBCASE("with using declaration") {
        const auto source = SourceFile("import mylib using some_func;", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("using mylib::some_func;"));
    }

    SUBCASE("with using wildcard") {
        const auto source = SourceFile("import mylib using *", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("using namespace mylib;"));
    }

    SUBCASE("std module emits no import statement") {
        const auto source = SourceFile("import std;", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(!cpp.contains("import std;"));
    }
}

TEST_CASE("Codegen: enum") {
    SUBCASE("basic enum") {
        const auto source = SourceFile("enum Color { Red, Green, Blue }", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("enum class Color {\n    Red,\n    Green,\n    Blue,\n};"));
    }

    SUBCASE("enum with size type") {
        const auto source = SourceFile("enum Color : u8 { Red, Green }", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("enum class Color : std::uint8_t"));
    }
}

TEST_CASE("Codegen: struct") {
    SUBCASE("basic struct") {
        const auto source = SourceFile("struct Point { x: i32, y: i32 }", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("struct Point {"));
        CHECK(cpp.contains("std::int32_t x;"));
        CHECK(cpp.contains("std::int32_t y;"));
    }
}

TEST_CASE("Codegen: function signatures") {
    SUBCASE("main function gets int return") {
        const auto cpp = gen("fn main() { }");
        CHECK(cpp.contains("auto main() noexcept -> int {"));
    }

    SUBCASE("function without return type") {
        const auto cpp = gen("fn foo() { }");
        CHECK(cpp.contains("auto foo() noexcept {"));
    }

    SUBCASE("function with return type") {
        const auto cpp = gen("fn add() -> i32 { }");
        CHECK(cpp.contains("auto add() noexcept -> std::int32_t {"));
    }

    SUBCASE("function with typed parameters") {
        const auto cpp = gen("fn add(a: i32, b: i32) -> i32 { }");
        CHECK(cpp.contains("auto add(std::int32_t a, std::int32_t b)"));
    }

    SUBCASE("function with untyped parameter") {
        const auto cpp = gen("fn foo(x) { }");
        CHECK(cpp.contains("auto foo(auto x) noexcept"));
    }

    SUBCASE("function marked noexcept") {
        const auto cpp = gen("fn f() { }");
        CHECK(cpp.contains("noexcept"));
    }
}

TEST_CASE("Codegen: variable declarations") {
    SUBCASE("let without type") {
        const auto cpp = gen("fn main() { let x = 5; }");
        CHECK(cpp.contains("const auto x = 5;"));
    }

    SUBCASE("let with cast-style init") {
        const auto cpp = gen("fn main() { let x: i32 = 5; }");
        CHECK(cpp.contains("const auto x = std::int32_t(5);"));
    }

    SUBCASE("var with init") {
        const auto cpp = gen("fn main() { var x = 10; }");
        CHECK(cpp.contains("auto x = 10;"));
    }

    SUBCASE("var with type and init") {
        const auto cpp = gen("fn main() { var x: i32 = 10; }");
        CHECK(cpp.contains("auto x = std::int32_t(10);"));
    }

    SUBCASE("var without init defaults to T()") {
        const auto cpp = gen("fn main() { var x: i32; }");
        CHECK(cpp.contains("auto x = std::int32_t();"));
    }

    SUBCASE("const declaration") {
        const auto cpp = gen("fn main() { const N = 42; }");
        CHECK(cpp.contains("constexpr auto N = 42;"));
    }
}

TEST_CASE("Codegen: return statements") {
    SUBCASE("return void") {
        const auto cpp = gen("fn main() { return; }");
        CHECK(cpp.contains("return;"));
    }

    SUBCASE("return with value") {
        const auto cpp = gen("fn main() { return 0; }");
        CHECK(cpp.contains("return 0;"));
    }
}

TEST_CASE("Codegen: while statements") {
    SUBCASE("while with block body") {
        const auto cpp = gen("fn main() { while true { return; } }");
        CHECK(cpp.contains("while (true)"));
    }

    SUBCASE("while with multi-statement block body") {
        const auto cpp = gen("fn main() { while true { a(); b(); } }");
        CHECK(cpp.contains("while (true) {\n        a();\n        b();\n    }"));
    }
}

TEST_CASE("Codegen: for statements") {
    SUBCASE("for with all clauses") {
        const auto cpp = gen("fn main() { for (var i = 0; i < 10; i++) { } }");
        CHECK(cpp.contains("for (auto i = 0; i < 10; i++)"));
    }

    SUBCASE("for with empty clauses") {
        const auto cpp = gen("fn main() { for ( ; ; ) { } }");
        CHECK(cpp.contains("for (; ; )"));
    }

    SUBCASE("for with multi-statement block body") {
        const auto cpp = gen("fn main() { for (var i = 0; i < 10; i++) { a(); b(); } }");
        CHECK(cpp.contains("for (auto i = 0; i < 10; i++) {\n        a();\n        b();\n    }"));
    }
}

TEST_CASE("Codegen: if expression") {
    SUBCASE("ternary optimization for single-expression branches") {
        const auto source = SourceFile("fn main() { let x = if true { 1 } else { 2 }; }", "<test>");
        const auto result = parse(tokenize(source.text()), source);
        const auto cpp = generate(result.items, source, 20, false);
        // Should optimize to ternary
        CHECK(cpp.contains("true ? 1 : 2"));
    }

    SUBCASE("if without else generates if block") {
        const auto cpp = gen("fn main() { if true { return; } }");
        CHECK(cpp.contains("if (true) {"));
    }
}

TEST_CASE("Codegen: expression generation") {
    SUBCASE("prefix negation") {
        const auto cpp = gen("fn main() { -x; }");
        CHECK(cpp.contains("-"));
    }

    SUBCASE("prefix logical not") {
        const auto cpp = gen("fn main() { !x; }");
        CHECK(cpp.contains("!"));
    }

    SUBCASE("prefix pre-increment") {
        const auto cpp = gen("fn main() { ++x; }");
        CHECK(cpp.contains("++"));
    }

    SUBCASE("postfix increment") {
        const auto cpp = gen("fn main() { x++; }");
        CHECK(cpp.contains("x++"));
    }

    SUBCASE("binary expression") {
        const auto cpp = gen("fn main() { a + b * c; }");
        CHECK(cpp.contains("a + b * c"));
    }

    SUBCASE("function call") {
        const auto cpp = gen("fn main() { foo(1, 2); }");
        CHECK(cpp.contains("foo(1, 2)"));
    }

    SUBCASE("index expression") {
        const auto cpp = gen("fn main() { arr[0]; }");
        CHECK(cpp.contains("arr[0]"));
    }

    SUBCASE("field access") {
        const auto cpp = gen("fn main() { obj.field; }");
        CHECK(cpp.contains("obj.field"));
    }

    SUBCASE("group expression") {
        const auto cpp = gen("fn main() { (a + b); }");
        CHECK(cpp.contains("(a + b)"));
    }

    SUBCASE("assignment") {
        const auto cpp = gen("fn main() { x = 42; }");
        CHECK(cpp.contains("x = 42"));
    }

    SUBCASE("compound assignment") {
        const auto cpp = gen("fn main() { x += 1; }");
        CHECK(cpp.contains("x += 1"));
    }

    SUBCASE("comma expression") {
        const auto cpp = gen("fn main() { a, b; }");
        CHECK(cpp.contains("a, b"));
    }

    SUBCASE("logical operators") {
        const auto cpp = gen("fn main() { a && b || c; }");
        CHECK(cpp.contains("&&"));
        CHECK(cpp.contains("||"));
    }

    SUBCASE("comparison operators") {
        const auto cpp = gen("fn main() { a == b; c != d; }");
        CHECK(cpp.contains("a == b"));
        CHECK(cpp.contains("c != d"));
    }

    SUBCASE("shift operators") {
        const auto cpp = gen("fn main() { a << b; }");
        CHECK(cpp.contains("a << b"));
    }

    SUBCASE("bitwise operators") {
        const auto cpp = gen("fn main() { a & b; c | d; e ^ f; }");
        CHECK(cpp.contains("a & b"));
        CHECK(cpp.contains("c | d"));
        CHECK(cpp.contains("e ^ f"));
    }
}

TEST_CASE("Codegen: helloworld full output") {
    const auto source = SourceFile(
        "import std;\n"
        "fn main() {\n"
        "    std::println(\"Hello World\");\n"
        "}\n"
    );
    const auto result = parse(tokenize(source.text()), source);
    const auto cpp = generate(result.items, source, 23, false);
    // With std module auto-detection, should include preamble
    CHECK(cpp.contains("auto main() noexcept -> int"));
    CHECK(cpp.contains("std::println(\"Hello World\")"));
}
