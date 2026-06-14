#include "test_helpers.h"

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
            .carven_program = {},
            .standard = 26,
            .import_std = false,
        });
        const auto b = make_single_file_config({
            .absolute_source_path = "src/hello.cv",
            .carven_program = {},
            .standard = 26,
            .import_std = false,
        });
        CHECK_EQ(a.root_dir, b.root_dir);
        CHECK_EQ(a.target_name, "hello");
        CHECK_EQ(a.absolute_source_path, "src/hello.cv");
        CHECK_EQ(a.standard, 26);
        CHECK(!a.import_std);
        CHECK(a.root_dir.contains("carven/scripts/hello-"));
    }

    SUBCASE("target name is sanitized") {
        const auto config = make_single_file_config({
            .absolute_source_path = "src/my-app.cv",
            .carven_program = {},
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

TEST_CASE("Toolchain: project root discovery") {
    const auto base = std::filesystem::temp_directory_path() / "carven_test_project_root";
    const auto nested = base / "a" / "b";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(nested);

    SUBCASE("finds parent xmake.lua") {
        std::ofstream(base / "xmake.lua") << "target(\"app\")\n";
        const auto root = find_project_root(nested);
        REQUIRE(root.has_value());
        CHECK_EQ(*root, base.generic_string());
    }

    SUBCASE("returns nullopt without xmake.lua") {
        const auto root = find_project_root(nested);
        CHECK(!root.has_value());
    }

    std::filesystem::remove_all(base);
}
