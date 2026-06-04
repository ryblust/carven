#include "test_helpers.h"

import carven.driver.pipeline;
import std;

TEST_CASE("Pipeline: parse_flags") {
    SUBCASE("defaults") {
        const auto args = std::array<const char*, 0>{};
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->language_standard, 23);
        CHECK(!driver->import_std);
        CHECK(!driver->emit_only);
        CHECK(driver->input_files.empty());
    }

    SUBCASE("parse -std=c++20") {
        const auto args = std::array { "-std=c++20" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->language_standard, 20);
    }

    SUBCASE("parse -std=c++14") {
        const auto args = std::array { "-std=c++14" };
        const auto driver = parse_flags(args);
        CHECK_EQ(driver->language_standard, 14);
    }

    SUBCASE("unknown standard returns nullopt") {
        const auto args = std::array { "-std=c++11" };
        const auto driver = parse_flags(args);
        CHECK(!driver.has_value());
    }

    SUBCASE("parse --output-dir") {
        const auto args = std::array { "--output-dir=build/cache" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->output_dir, "build/cache");
    }

    SUBCASE("boolean flags") {
        CHECK(parse_flags(std::array { "--import-std" })->import_std);
        CHECK(parse_flags(std::array { "-E" })->emit_only);
        CHECK(parse_flags(std::array { "--only-tokens" })->only_tokens);
        CHECK(parse_flags(std::array { "--only-ast" })->only_ast);
    }

    SUBCASE("unknown flag returns nullopt") {
        const auto args = std::array { "--unknown" };
        const auto driver = parse_flags(args);
        CHECK(!driver.has_value());
    }

    SUBCASE("input files collected") {
        const auto args = std::array { "file1.cv", "file2.cv" };
        const auto driver = parse_flags(args);
        CHECK(driver.has_value());
        CHECK_EQ(driver->input_files.size(), 2u);
        CHECK_EQ(driver->input_files[0], "file1.cv");
        CHECK_EQ(driver->input_files[1], "file2.cv");
    }

    SUBCASE("multiple flags combined") {
        const auto args = std::array {
            "-std=c++20", "--import-std", "-E", "main.cv",
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
