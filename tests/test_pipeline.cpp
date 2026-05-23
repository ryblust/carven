#include "doctest.h"

import carven.driver.pipeline;
import carven.common.source;
import std;

TEST_CASE("Pipeline: parse_flags") {
    SUBCASE("defaults") {
        const auto args = std::array<const char*, 0> {};
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->language_standard, 23);
        CHECK(!driver->import_std);
        CHECK(!driver->emit_only);
        CHECK(driver->input_files.empty());
    }

    SUBCASE("parse -std=c++20") {
        const auto args = std::array<const char*, 1> { "-std=c++20" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->language_standard, 20);
    }

    SUBCASE("parse -std=c++14") {
        const auto args = std::array<const char*, 1> { "-std=c++14" };
        const auto driver = parse_flags(args);
        CHECK_EQ(driver->language_standard, 14);
    }

    SUBCASE("parse -std=c++17") {
        const auto args = std::array<const char*, 1> { "-std=c++17" };
        const auto driver = parse_flags(args);
        CHECK_EQ(driver->language_standard, 17);
    }

    SUBCASE("parse -std=c++23") {
        const auto args = std::array<const char*, 1> { "-std=c++23" };
        const auto driver = parse_flags(args);
        CHECK_EQ(driver->language_standard, 23);
    }

    SUBCASE("parse -std=c++26") {
        const auto args = std::array<const char*, 1> { "-std=c++26" };
        const auto driver = parse_flags(args);
        CHECK_EQ(driver->language_standard, 26);
    }

    SUBCASE("unknown standard returns nullopt") {
        const auto args = std::array<const char*, 1> { "-std=c++11" };
        const auto driver = parse_flags(args);
        CHECK(!driver.has_value());
    }

    SUBCASE("parse --output-dir") {
        const auto args = std::array<const char*, 1> { "--output-dir=build/cache" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->output_dir, "build/cache");
    }

    SUBCASE("parse --import-std") {
        const auto args = std::array<const char*, 1> { "--import-std" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK(driver->import_std);
    }

    SUBCASE("parse -E (emit only)") {
        const auto args = std::array<const char*, 1> { "-E" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK(driver->emit_only);
    }

    SUBCASE("parse --only-tokens") {
        const auto args = std::array<const char*, 1> { "--only-tokens" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK(driver->only_tokens);
    }

    SUBCASE("parse --only-ast") {
        const auto args = std::array<const char*, 1> { "--only-ast" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK(driver->only_ast);
    }

    SUBCASE("unknown flag returns nullopt") {
        const auto args = std::array<const char*, 1> { "--unknown" };
        const auto driver = parse_flags(args);
        CHECK(!driver.has_value());
    }

    SUBCASE("input files collected") {
        const auto args = std::array<const char*, 2> { "file1.cv", "file2.cv" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->input_files.size(), 2u);
        CHECK_EQ(driver->input_files[0], "file1.cv");
        CHECK_EQ(driver->input_files[1], "file2.cv");
    }

    SUBCASE("multiple flags combined") {
        const auto args = std::array<const char*, 4> {
            "-std=c++20", "--import-std", "-E", "main.cv"
        };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->language_standard, 20);
        CHECK(driver->import_std);
        CHECK(driver->emit_only);
        CHECK_EQ(driver->input_files.size(), 1u);
        CHECK_EQ(driver->input_files[0], "main.cv");
    }
}

TEST_CASE("Pipeline: transpile") {
    SUBCASE("transpile simple function") {
        const auto driver = Driver {};
        static constexpr auto source = std::string_view("fn main() { }");
        const auto result = transpile(driver, source);
        CHECK(result.errors.empty());
        CHECK(!result.output.empty());
    }

    SUBCASE("transpile includes preamble when import_std is set") {
        auto driver = Driver {};
        driver.import_std = true;
        static constexpr auto source = std::string_view("fn main() { }");
        const auto result = transpile(driver, source);
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile auto-detects import std") {
        const auto driver = Driver {};
        static constexpr auto source = std::string_view("import std;\nfn main() { }");
        const auto result = transpile(driver, source);
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile returns errors on invalid source") {
        const auto driver = Driver {};
        static constexpr auto source = std::string_view("@ invalid @");
        const auto result = transpile(driver, source);
        CHECK(!result.errors.empty());
    }

    SUBCASE("transpile with c++26 standard") {
        auto driver = Driver {};
        driver.language_standard = 26;
        driver.import_std = true;
        static constexpr auto source = std::string_view("fn main() { }");
        const auto result = transpile(driver, source);
        CHECK(result.errors.empty());
    }

    SUBCASE("transpile helloworld") {
        const auto driver = Driver {};
        static constexpr auto source = std::string_view(
            "import std;\n"
            "fn main() {\n"
            "    std::println(\"Hello World\");\n"
            "}\n"
        );
        const auto result = transpile(driver, source);
        CHECK(result.errors.empty());
        CHECK(result.output.contains("auto main() noexcept -> int"));
        CHECK(result.output.contains("std::println(\"Hello World\")"));
    }
}
