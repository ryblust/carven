#include "test_helpers.h"

import carven.driver.command.dump;
import carven.driver.request;
import std;

TEST_CASE("Dump: tokens and AST") {
    SUBCASE("dump valid file") {
        const auto request = DumpRequest {
            .source_file = "tests/fixtures/helloworld.cv",
        };
        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request); });
        CHECK_EQ(result, 0);
    }

    SUBCASE("dump nonexistent file returns 1") {
        const auto request = DumpRequest {
            .source_file = "nonexistent_file_for_test_123.cv",
        };
        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request); });
        CHECK_EQ(result, 1);
    }

    SUBCASE("dump with --only-tokens") {
        const auto request = DumpRequest {
            .source_file = "tests/fixtures/helloworld.cv",
            .only_tokens = true,
        };
        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request); });
        CHECK_EQ(result, 0);
    }

    SUBCASE("dump with --only-ast") {
        const auto request = DumpRequest {
            .source_file = "tests/fixtures/helloworld.cv",
            .only_ast = true,
        };
        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request); });
        CHECK_EQ(result, 0);
    }
}
