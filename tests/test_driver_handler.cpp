#include "test_helpers.h"

import carven.driver.command.init;
import carven.driver.command.run;
import carven.driver.command.transpile;
import carven.driver.request;
import std;

TEST_CASE("Command executor: run") {
    SUBCASE("nonexistent single file returns 1") {
        const auto request = RunRequest {
            .mode = SingleFileRun {
                .source_file = "nonexistent_file_for_test_123.cv",
                .args = {},
            },
        };
        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request, "/tmp/carven"); });
        CHECK_EQ(result, 1);
    }
}

TEST_CASE("Command executor: transpile") {
    SUBCASE("missing file returns error") {
        const auto request = TranspileRequest {
            .output_file = std::nullopt,
            .source_files = std::vector<std::string_view> { "nonexistent_file_for_test_123.cv" },
        };
        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request); });
        CHECK_EQ(result, 1);
    }
}

TEST_CASE("Command executor: init") {
    const auto base = std::filesystem::temp_directory_path() / "carven_test_init";
    std::filesystem::remove_all(base);

    SUBCASE("creates project files") {
        const auto path = base.string();
        const auto request = InitRequest {
            .project_dir = path,
        };

        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request, "/tmp/carven"); });
        CHECK_EQ(result, 0);
        CHECK(std::filesystem::exists(base / "src" / "main.cv"));
        CHECK(std::filesystem::exists(base / "xmake.lua"));
        CHECK(std::filesystem::exists(base / "xmake" / "rules" / "carven.lua"));
        CHECK(!std::filesystem::exists(base / ".carven"));
    }

    SUBCASE("refuses non-empty directory") {
        std::filesystem::create_directories(base);
        std::ofstream(base / "existing.txt") << "content";

        const auto path = base.string();
        const auto request = InitRequest {
            .project_dir = path,
        };

        const auto result = carven_test::run_silenced([&]() noexcept { return execute(request, "/tmp/carven"); });
        CHECK_EQ(result, 1);
    }

    std::filesystem::remove_all(base);
}
