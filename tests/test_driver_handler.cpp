#include "test_helpers.h"

import carven.driver.pipeline;
import carven.driver.handler;
import std;

TEST_CASE("Handler: run") {
    SUBCASE("nonexistent file returns 1") {
        auto driver = Driver{};
        driver.input_files.push_back("nonexistent_file_for_test_123.cv");
        const auto result = carven_test::run_silenced([&]() noexcept { return run(driver); });
        CHECK_EQ(result, 1);
    }

    SUBCASE("project runtime args require an explicit target") {
        auto driver = Driver{};
        driver.forwarded_args.push_back("--version");
        const auto result = carven_test::run_silenced([&]() noexcept { return run_project(driver); });
        CHECK_EQ(result, 1);
    }
}

TEST_CASE("Handler: transpile") {
    SUBCASE("requires input") {
        const auto driver = Driver{};
        const auto result = carven_test::run_silenced([&]() noexcept { return transpile_command(driver); });
        CHECK_EQ(result, 1);
    }

    SUBCASE("missing file returns error") {
        auto driver = Driver{};
        driver.input_files.push_back("nonexistent_file_for_test_123.cv");
        const auto result = carven_test::run_silenced([&]() noexcept { return transpile_command(driver); });
        CHECK_EQ(result, 1);
    }
}

TEST_CASE("Handler: init") {
    const auto base = std::filesystem::temp_directory_path() / "carven_test_init";
    std::filesystem::remove_all(base);

    SUBCASE("creates project files") {
        const auto path = base.string();
        auto driver = Driver{};
        driver.carven_executable = "/tmp/carven";
        driver.input_files.push_back(path);

        const auto result = carven_test::run_silenced([&]() noexcept { return init(driver); });
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
        auto driver = Driver{};
        driver.input_files.push_back(path);

        const auto result = carven_test::run_silenced([&]() noexcept { return init(driver); });
        CHECK_EQ(result, 1);
    }

    std::filesystem::remove_all(base);
}
