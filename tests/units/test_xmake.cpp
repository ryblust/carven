#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include "doctest.h"

import carven.driver.xmake;
import std;

TEST_CASE("Driver xmake: target names") {
    SUBCASE("target name is sanitized") {
        CHECK_EQ(sanitize_xmake_target_name("my-app"), "my_app");
        CHECK_EQ(sanitize_xmake_target_name("123"), "app_123");
        CHECK_EQ(sanitize_xmake_target_name(""), "app_");
    }
}

TEST_CASE("Driver xmake: generated project uses project name target") {
    const auto sources = std::vector<std::string> { "src/main.cv" };
    const auto content = generate_xmake_project_file("hello", sources, {
        .language_standard = 23,
        .import_std = false
    });
    CHECK(content.contains("set_project(\"hello\")"));
    CHECK(content.contains("target(\"hello\")"));
    CHECK(!content.contains("target(\"app\")"));
}

TEST_CASE("Driver xmake: generated project can force import std") {
    const auto sources = std::vector<std::string> { "src/main.cv" };
    const auto content = generate_xmake_project_file("hello", sources, {
        .language_standard = 23,
        .import_std = true
    });
    CHECK(content.contains("set_values(\"carven.import_std\", true)"));
}

TEST_CASE("Driver xmake: writes embedded carven rule") {
    auto dir = std::filesystem::temp_directory_path() / "carven-test-xmake-rule";
    std::filesystem::remove_all(dir);

    CHECK(write_carven_xmake_rule(dir));

    auto error = std::error_code();
    const auto rule = std::filesystem::path(dir) / "xmake" / "rules" / "carven.lua";
    CHECK(std::filesystem::is_regular_file(rule, error));
    CHECK(!error);

    auto stream = std::ifstream(rule);
    auto content = std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    );
    CHECK(content.contains("rule(\"carven\")"));
    CHECK(content.contains("set_extensions(\".cv\")"));
    CHECK(content.contains("on_buildcmd_file"));

    std::filesystem::remove_all(dir);
}
