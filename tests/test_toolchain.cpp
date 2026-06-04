#include "test_helpers.h"

import carven.driver.toolchain;
import std;

TEST_CASE("Toolchain: build_artifacts") {
    SUBCASE("filenames resolve correctly") {
        CHECK(build_artifacts("hello.cv", "/tmp/out").cpp_path.string().ends_with("hello.cpp"));
        CHECK(build_artifacts("src/foo.cv", "/tmp/out").cpp_path.string().ends_with("foo.cpp"));
        CHECK(build_artifacts("foo.bar.cv", "/tmp/out").cpp_path.string().ends_with("foo.bar.cpp"));
    }
}

TEST_CASE("Toolchain: needs_compile") {
    const auto base = std::filesystem::temp_directory_path() / "carven_test_compile";
    std::filesystem::create_directories(base);

    SUBCASE("exe does not exist returns true") {
        const auto cpp  = base / "test.cpp";
        const auto exe  = base / "test";
        std::ofstream(cpp) << "content";
        CHECK(needs_compile(cpp, exe));
    }

    SUBCASE("cpp newer than exe returns true") {
        const auto cpp = base / "a.cpp";
        const auto exe = base / "a";
        std::ofstream(cpp) << "content";
        std::ofstream(exe) << "binary";
        std::filesystem::last_write_time(cpp, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1));
        CHECK(needs_compile(cpp, exe));
    }

    SUBCASE("exe newer than cpp returns false") {
        const auto cpp = base / "b.cpp";
        const auto exe = base / "b";
        std::ofstream(cpp) << "content";
        std::ofstream(exe) << "binary";
        std::filesystem::last_write_time(exe, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1));
        CHECK(!needs_compile(cpp, exe));
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("Toolchain: compiler_from_environment") {
    const auto saved = std::getenv("CXX");
    const auto saved_str = saved ? std::string(saved) : std::string();

    SUBCASE("CXX not set uses fallback") {
        unsetenv("CXX");
        CHECK_EQ(compiler_from_environment("default-cc"), "default-cc");
    }

    SUBCASE("CXX set uses env value") {
        setenv("CXX", "my-compiler", 1);
        CHECK_EQ(compiler_from_environment("default-cc"), "my-compiler");
    }

    // Restore
    if (!saved_str.empty()) {
        setenv("CXX", saved_str.c_str(), 1);
    } else {
        unsetenv("CXX");
    }
}
