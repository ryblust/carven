#include "test_helpers.h"

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

TEST_CASE("Codegen: include preamble and imports") {
    SUBCASE("preamble follows requested standard") {
        CHECK(!gen("fn main() { }", false).contains("#include"));
        CHECK(gen("fn main() { }", true, 14).contains("#include <vector>"));
        CHECK(gen("fn main() { }", true, 17).contains("#include <optional>"));
        CHECK(gen("fn main() { }", true, 20).contains("#include <span>"));
        CHECK(gen("fn main() { }", true, 23).contains("#include <print>"));
        CHECK(gen("fn main() { }", true, 26).contains("#include"));
    }

    SUBCASE("std import is header fallback") {
        const auto cpp = gen("import std;", true, 23);
        CHECK(cpp.contains("#include <print>"));
        CHECK(!cpp.contains("import std;"));
    }

    SUBCASE("non-std imports pass through") {
        const auto cpp = gen(
            "import mylib;\n"
            "import tools using run;\n"
            "import extras using *;\n"
        );
        CHECK(cpp.contains("import mylib;"));
        CHECK(cpp.contains("using tools::run;"));
        CHECK(cpp.contains("using namespace extras;"));
    }
}

TEST_CASE("Codegen: builtin type mapping") {
    constexpr auto cases = std::array<std::pair<std::string_view, std::string_view>, 12> {
        std::pair { "i8", "std::int8_t" },
        std::pair { "i16", "std::int16_t" },
        std::pair { "i32", "std::int32_t" },
        std::pair { "i64", "std::int64_t" },
        std::pair { "u8", "std::uint8_t" },
        std::pair { "u16", "std::uint16_t" },
        std::pair { "u32", "std::uint32_t" },
        std::pair { "u64", "std::uint64_t" },
        std::pair { "usize", "std::size_t" },
        std::pair { "f32", "float" },
        std::pair { "f64", "double" },
        std::pair { "MyType", "MyType" },
    };

    for (const auto [carven_type, cpp_type] : cases) {
        const auto source = std::format("fn main() {{ var x: {} = value; }}", carven_type);
        const auto cpp = gen(source);
        CAPTURE(carven_type);
        CHECK(cpp.contains(cpp_type));
    }
}

TEST_CASE("Codegen: function signatures") {
    CHECK(gen("fn main() { }").contains("auto main() noexcept -> int {"));
    CHECK(gen("fn add(a: i32, b: i32) -> i32 { }").contains("auto add(std::int32_t a, std::int32_t b) noexcept -> std::int32_t"));
    CHECK(gen("fn id(value) { }").contains("auto id(auto value) noexcept {"));

    const auto cpp = gen("fn main(args) { }");
    CHECK(cpp.contains("#include <ranges>"));
    CHECK(cpp.contains("auto main(int __carven_argc, char** __carven_argv) noexcept -> int {"));
    CHECK(cpp.contains("const auto args ="));
    CHECK(cpp.contains("std::views::iota(1, __carven_argc)"));
    CHECK(cpp.contains("std::views::transform([__carven_argv](int index) noexcept"));
    CHECK(cpp.contains("std::pair<std::size_t, std::string_view>"));
}

TEST_CASE("Codegen: declarations") {
    constexpr auto source =
        "fn main() {\n"
        "    let a = 1;\n"
        "    let b: i32 = 2;\n"
        "    var c = 3;\n"
        "    var d: i32;\n"
        "    const E = 4;\n"
        "}\n";
    const auto cpp = gen(source);

    CHECK(cpp.contains("const auto a = 1;"));
    CHECK(cpp.contains("const auto b = std::int32_t(2);"));
    CHECK(cpp.contains("auto c = 3;"));
    CHECK(cpp.contains("auto d = std::int32_t();"));
    CHECK(cpp.contains("constexpr auto E = 4;"));
}

TEST_CASE("Codegen: arrays") {
    const auto cpp = gen(
        "struct Matrix { data: [f64; 16] }\n"
        "fn sum(data: [i32; 3]) -> i32 { return data[0]; }\n"
        "fn main() { let a: [i32; 3] = [1, 2, 3]; let m = [[1, 2], [3, 4]]; }\n"
    );

    CHECK(cpp.contains("std::array<double, 16> data;"));
    CHECK(cpp.contains("auto sum(std::array<std::int32_t, 3> data) noexcept -> std::int32_t"));
    CHECK(cpp.contains("const auto a = std::array<std::int32_t, 3>(std::array { 1, 2, 3 });"));
    CHECK(cpp.contains("const auto m = std::array { std::array { 1, 2 }, std::array { 3, 4 } };"));
}

TEST_CASE("Codegen: value if lowers to IIFE") {
    const auto cpp = gen("fn main() { let x = if true { work(); 1 } else { 2 }; }");

    CHECK(cpp.contains("const auto x = ([&]() noexcept {"));
    CHECK(cpp.contains("work();"));
    CHECK(cpp.contains("return 1;"));
    CHECK(cpp.contains("return 2;"));
    CHECK(!cpp.contains(" ? "));
}

TEST_CASE("Codegen: match lowering") {
    SUBCASE("value match evaluates scrutinee once") {
        const auto cpp = gen("fn main() { let a = match x { 1 | 2 => { 10 } _ => { 0 } }; }");
        CHECK(cpp.contains("const auto a = ([&]() noexcept {"));
        CHECK(cpp.contains("auto&& __carven_match_0 = x;"));
        CHECK(cpp.contains("if (__carven_match_0 == 1 || __carven_match_0 == 2)"));
        CHECK(cpp.contains("return 10;"));
        CHECK(cpp.contains("return 0;"));
    }

    SUBCASE("type and mixed patterns") {
        const auto cpp = gen("fn f(v) { match v { 0 => { zero; } i32 | f64 => { typed; } _ => { other; } } }");
        CHECK(cpp.contains("__carven_match_0 == 0"));
        CHECK(cpp.contains("if constexpr"));
        CHECK(cpp.contains("std::is_same_v<std::remove_cvref_t<decltype(__carven_match_0)>, std::int32_t>"));
        CHECK(cpp.contains("std::is_same_v<std::remove_cvref_t<decltype(__carven_match_0)>, double>"));
    }
}
