#include "test_helpers.h"

import carven.common.filesystem;
import std;

TEST_CASE("Filesystem: ensure_directory") {
    const auto base = std::filesystem::temp_directory_path();
    const auto dir = base / "carven_test_dir";

    std::filesystem::remove_all(dir);

    SUBCASE("creates new directory") {
        CHECK(ensure_directory(dir));
        CHECK(std::filesystem::exists(dir));
    }

    SUBCASE("succeeds when directory already exists") {
        std::filesystem::create_directories(dir);
        CHECK(ensure_directory(dir));
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("Filesystem: write_file_if_changed") {
    const auto base = std::filesystem::temp_directory_path() / "carven_test_write";
    const auto path = base / "test.cpp";
    std::filesystem::create_directories(base);

    SUBCASE("first write succeeds") {
        std::filesystem::remove(path);
        CHECK(write_file_if_changed(path, "int x = 1;"));
        CHECK(std::filesystem::exists(path));

        auto in = std::ifstream(path);
        const auto content = std::string(std::istreambuf_iterator<char>(in), {});
        CHECK_EQ(content, "int x = 1;");
    }

    SUBCASE("same content skips write") {
        write_file_if_changed(path, "int x = 2;");
        CHECK(write_file_if_changed(path, "int x = 2;"));
        auto in = std::ifstream(path);
        const auto content = std::string(std::istreambuf_iterator<char>(in), {});
        CHECK_EQ(content, "int x = 2;");
    }

    SUBCASE("different content overwrites") {
        write_file_if_changed(path, "int x = 3;");
        CHECK(write_file_if_changed(path, "int x = 4;"));
        auto in = std::ifstream(path);
        const auto content = std::string(std::istreambuf_iterator<char>(in), {});
        CHECK_EQ(content, "int x = 4;");
    }

    SUBCASE("write to read-only directory fails") {
        const auto ro_dir = base / "readonly";
        std::filesystem::create_directories(ro_dir);
        std::filesystem::permissions(ro_dir, std::filesystem::perms::none);
        CHECK(!write_file_if_changed(ro_dir / "test.cpp", "data"));
        std::filesystem::permissions(ro_dir, std::filesystem::perms::owner_all);
        std::filesystem::remove_all(ro_dir);
    }

    std::filesystem::remove_all(base);
}
