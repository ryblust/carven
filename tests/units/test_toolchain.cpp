#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include "doctest.h"

import carven.driver.toolchain;
import std;

TEST_CASE("Toolchain: carven source paths") {
    CHECK(is_carven_source_path("main.cv"));
    CHECK(is_carven_source_path("src/main.cv"));
    CHECK(!is_carven_source_path("main.cpp"));
    CHECK(!is_carven_source_path("cv"));
}

TEST_CASE("Toolchain: single file config") {
    SUBCASE("project path is stable") {
        const auto a = make_single_file_config({
            .absolute_source_path = "src/hello.cv",
            .standard = 26,
            .import_std = false,
        });
        const auto b = make_single_file_config({
            .absolute_source_path = "src/hello.cv",
            .standard = 26,
            .import_std = false,
        });
        CHECK_EQ(a.root_dir, b.root_dir);
        CHECK_EQ(a.target_name, "hello");
        CHECK_EQ(a.absolute_source_path, "src/hello.cv");
        CHECK_EQ(a.standard, 26);
        CHECK(!a.import_std);
        const auto expected_prefix = (std::filesystem::current_path() / ".carven" / "scripts" / "hello-").generic_string();
        CHECK(a.root_dir.starts_with(expected_prefix));
    }

    SUBCASE("target name is sanitized") {
        const auto config = make_single_file_config({
            .absolute_source_path = "src/my-app.cv",
            .standard = 26,
            .import_std = false,
        });
        CHECK_EQ(config.target_name, "my_app");
    }
}

TEST_CASE("Toolchain: xmake args") {
    SUBCASE("build args") {
        const auto args = xmake_build_args("app");
        REQUIRE_EQ(args.size(), 5u);
        CHECK_EQ(args[0], "xmake");
        CHECK_EQ(args[1], "build");
        CHECK_EQ(args[2], "-F");
        CHECK_EQ(args[3], "xmake.lua");
        CHECK_EQ(args[4], "app");
    }

    SUBCASE("run args with forwarded args") {
        const auto forwarded = std::vector<std::string_view> { "--name", "Ada" };
        const auto args = xmake_run_args("app", forwarded);
        REQUIRE_EQ(args.size(), 7u);
        CHECK_EQ(args[0], "xmake");
        CHECK_EQ(args[1], "run");
        CHECK_EQ(args[2], "-F");
        CHECK_EQ(args[3], "xmake.lua");
        CHECK_EQ(args[4], "app");
        CHECK_EQ(args[5], "--name");
        CHECK_EQ(args[6], "Ada");
    }
}

TEST_CASE("Toolchain: generated project xmake uses project name target") {
    const auto sources = std::vector<std::string> { "src/main.cv" };
    const auto content = generate_xmake_project("hello", sources, 23);
    CHECK(content.contains("set_project(\"hello\")"));
    CHECK(content.contains("target(\"hello\")"));
    CHECK(!content.contains("target(\"app\")"));
}

TEST_CASE("Toolchain: project root discovery") {
    SUBCASE("finds current xmake.lua") {
        const auto root = find_project_root(".");
        REQUIRE(root.has_value());
        CHECK_EQ(*root, std::filesystem::absolute(".").generic_string());
    }

    SUBCASE("does not search parent directories") {
        const auto root = find_project_root("tests/units");
        CHECK(!root.has_value());
    }
}

TEST_CASE("Toolchain: embedded carven rule") {
    const auto rule = embedded_carven_rule();
    CHECK(!rule.empty());
    CHECK(rule.contains("rule(\"carven\")"));
    CHECK(rule.contains("carven.build.cv"));
}

TEST_CASE("Toolchain: generated single file xmake does not pin carven program") {
    const auto config = make_single_file_config({
        .absolute_source_path = "src/hello.cv",
        .standard = 23,
        .import_std = false,
    });

    const auto content = generate_xmake_single_file(config);
    CHECK(content.contains("includes(\"xmake/rules/carven.lua\")"));
    CHECK(!content.contains("carven.program"));
}
