#include "test_helpers.h"

import carven.common.source;
import carven.frontend.token;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import carven.backend.codegen;
import std;

static auto gen(std::string_view source_text, bool import_std = false, std::uint8_t standard = 23) noexcept -> std::string {
    const auto result = parse(tokenize(source_text), source_text);
    return generate(result.items, source_text, standard, import_std);
}

TEST_CASE("Codegen: type mapping") {
    SUBCASE("var with typed init") {
        CHECK(gen("fn main() { var x: i32 = 42; }").contains("std::int32_t(42)"));
        CHECK(gen("fn main() { var x: f64 = 3.14; }").contains("double(3.14)"));
        CHECK(gen("fn main() { var x: u8 = 255; }").contains("std::uint8_t(255)"));
        CHECK(gen("fn main() { var x: usize = 0; }").contains("std::size_t(0)"));
    }

    SUBCASE("let with i64 type") {
        const auto cpp = gen("fn main() { let x: i64 = 100; }");
        CHECK(cpp.contains("const auto x = std::int64_t(100)"));
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
        const auto source = "import mylib;";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("import mylib;"));
    }

    SUBCASE("std module emits no import statement") {
        const auto source = "import std;";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(!cpp.contains("import std;"));
    }

    SUBCASE("using declaration and wildcard") {
        CHECK(gen("import mylib using some_func;").contains("using mylib::some_func;"));
        CHECK(gen("import mylib using *").contains("using namespace mylib;"));
    }
}

TEST_CASE("Codegen: enum") {
    SUBCASE("basic enum") {
        const auto source = "enum Color { Red, Green, Blue }";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("enum class Color {\n    Red,\n    Green,\n    Blue,\n};"));
    }

    SUBCASE("enum with size type") {
        const auto source = "enum Color : u8 { Red, Green }";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("enum class Color : std::uint8_t"));
    }
}

TEST_CASE("Codegen: struct") {
    SUBCASE("basic struct") {
        const auto source = "struct Point { x: i32, y: i32 }";
        const auto result = parse(tokenize(source), source);
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
        const auto source = "fn main() { let x = if true { 1 } else { 2 }; }";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 20, false);
        CHECK(cpp.contains("true ? 1 : 2"));
    }

    SUBCASE("if without else generates if block") {
        const auto cpp = gen("fn main() { if true { return; } }");
        CHECK(cpp.contains("if (true) {"));
    }
}

TEST_CASE("Codegen: helloworld full output") {
    auto source = SourceFile::from_file("tests/fixtures/helloworld.cv");
    REQUIRE(source.has_value());
    const auto text = source->text();
    const auto result = parse(tokenize(text), text);
    const auto cpp = generate(result.items, text, 23, false);
    CHECK(cpp.contains("auto main() noexcept -> int"));
    CHECK(cpp.contains("std::println(\"Hello World\")"));
}

TEST_CASE("Codegen: all type mappings") {
    SUBCASE("all Carven types map to correct C++ type") {
        CHECK(gen("fn main() { var x: i8 = 1; }").contains("std::int8_t"));
        CHECK(gen("fn main() { var x: i16 = 1; }").contains("std::int16_t"));
        CHECK(gen("fn main() { var x: u16 = 1; }").contains("std::uint16_t"));
        CHECK(gen("fn main() { var x: u32 = 1; }").contains("std::uint32_t"));
        CHECK(gen("fn main() { var x: u64 = 1; }").contains("std::uint64_t"));
        CHECK(gen("fn main() { var x: bool = true; }").contains("bool"));
        CHECK(gen("fn main() { var x: f32 = 1.0; }").contains("float"));
        CHECK(gen("fn main() { var x: char = 'a'; }").contains("char"));
        CHECK(gen("fn main() { var x: MyType = something; }").contains("MyType"));
    }
}

TEST_CASE("Codegen: empty types") {
    SUBCASE("empty enum") {
        const auto source = "enum Void {}";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 23, false);
        CHECK(cpp.contains("enum class Void {\n};"));
    }
    SUBCASE("empty struct") {
        const auto source = "struct Empty {}";
        const auto result = parse(tokenize(source), source);
        const auto cpp = generate(result.items, source, 23, false);
        CHECK(cpp.contains("struct Empty {\n};"));
    }
}

TEST_CASE("Codegen: nested control flow") {
    SUBCASE("if inside while") {
        const auto cpp = gen("fn main() { while true { if x { return; } } }");
        CHECK(cpp.contains("while"));
        CHECK(cpp.contains("if"));
    }
    SUBCASE("for inside if") {
        const auto cpp = gen("fn main() { if x { for ( ; ; ) { } } }");
        CHECK(cpp.contains("if"));
        CHECK(cpp.contains("for"));
    }
    SUBCASE("while inside for") {
        const auto cpp = gen("fn main() { for ( ; ; ) { while true { } } }");
        CHECK(cpp.contains("for"));
        CHECK(cpp.contains("while"));
    }
}

TEST_CASE("Codegen: return complex expression") {
    const auto cpp = gen("fn main() { return a + b * c; }");
    CHECK(cpp.contains("return a + b * c;"));
}

TEST_CASE("Codegen: array literals") {
    SUBCASE("simple array literal") {
        const auto cpp = gen("fn main() { let a = [1, 2, 3]; }");
        CHECK(cpp.contains("std::array { 1, 2, 3 }"));
    }
    SUBCASE("single element array") {
        const auto cpp = gen("fn main() { let a = [42]; }");
        CHECK(cpp.contains("std::array { 42 }"));
    }
    SUBCASE("nested array") {
        const auto cpp = gen("fn main() { let m = [[1, 2], [3, 4]]; }");
        CHECK(cpp.contains("std::array { std::array { 1, 2 }, std::array { 3, 4 } }"));
    }
}

TEST_CASE("Codegen: array type annotation") {
    SUBCASE("let with array type") {
        const auto cpp = gen("fn main() { let a: [i32; 3] = [1, 2, 3]; }");
        CHECK(cpp.contains("std::array<std::int32_t, 3>"));
    }
    SUBCASE("function parameter array type") {
        const auto cpp = gen("fn f(p: [i32; 3]) { }");
        CHECK(cpp.contains("std::array<std::int32_t, 3> p"));
    }
    SUBCASE("function return array type") {
        const auto cpp = gen("fn f() -> [i32; 3] { }");
        CHECK(cpp.contains("-> std::array<std::int32_t, 3>"));
    }
    SUBCASE("struct field array type") {
        const auto cpp = gen("struct S { f: [f64; 4] }");
        CHECK(cpp.contains("std::array<double, 4>"));
    }
    SUBCASE("user-defined type array") {
        const auto cpp = gen("fn main() { let a: [Point; 5] = [x, x]; }");
        CHECK(cpp.contains("std::array<Point, 5>"));
    }
}

TEST_CASE("Codegen: match expression") {
    SUBCASE("value match ternary") {
        const auto cpp = gen("fn main() { let a = match x { 1 => { 10 } _ => { 0 } }; }");
        CHECK(cpp.contains("x == 1 ? 10 : 0"));
    }
    SUBCASE("value match flat if") {
        const auto cpp = gen("fn main() { match x { 1 => { a; b; } _ => { c; } } }");
        CHECK(cpp.contains("if (x == 1)"));
        CHECK(cpp.contains("else"));
    }
    SUBCASE("type match if constexpr") {
        const auto cpp = gen("fn f(v) { match v { i32 => { a } _ => { b } } }");
        CHECK(cpp.contains("if constexpr"));
        CHECK(cpp.contains("std::is_same_v"));
        CHECK(cpp.contains("std::int32_t"));
    }
    SUBCASE("multi-pattern value") {
        const auto cpp = gen("fn main() { match x { 1 | 2 => { a } _ => { b } } }");
        CHECK(cpp.contains("||"));
    }
    SUBCASE("multi-pattern type") {
        const auto cpp = gen("fn f(v) { match v { i32 | f64 => { a } _ => { b } } }");
        CHECK(cpp.contains("std::is_same_v<std::remove_cvref_t<decltype(v)>, std::int32_t>"));
        CHECK(cpp.contains("std::is_same_v<std::remove_cvref_t<decltype(v)>, double>"));
    }
    SUBCASE("mixed patterns") {
        const auto cpp = gen("fn f(v) { match v { 0 => { a } i32 => { b } _ => { c } } }");
        CHECK(cpp.contains("std::is_same_v<std::remove_cvref_t<decltype(v)>, std::int32_t>"));
        CHECK(cpp.contains("v == 0"));
    }
}
