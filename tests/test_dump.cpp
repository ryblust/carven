#include "test_helpers.h"

import carven.driver.dump;
import carven.driver.pipeline;
import std;

TEST_CASE("Dump: tokens and AST") {
    SUBCASE("dump valid file") {
        auto driver = Driver{};
        driver.input_files.push_back("tests/fixtures/helloworld.cv");
        CHECK_EQ(dump(driver), 0);
    }

    SUBCASE("dump nonexistent file returns 1") {
        auto driver = Driver{};
        driver.input_files.push_back("nonexistent_file_for_test_123.cv");
        CHECK_EQ(dump(driver), 1);
    }

    SUBCASE("dump with --only-tokens") {
        auto driver = Driver{};
        driver.input_files.push_back("tests/fixtures/helloworld.cv");
        driver.only_tokens = true;
        CHECK_EQ(dump(driver), 0);
    }

    SUBCASE("dump with --only-ast") {
        auto driver = Driver{};
        driver.input_files.push_back("tests/fixtures/helloworld.cv");
        driver.only_ast = true;
        CHECK_EQ(dump(driver), 0);
    }
}
