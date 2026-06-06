#include "test_helpers.h"

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.toolchain;
import carven.common.filesystem;
import carven.common.process;
import std;

static auto run_and_capture(std::string cmd) noexcept -> std::pair<int, std::string> {
    auto pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {-1, ""};
    std::string stdout;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) stdout += buf;
    const auto status = pclose(pipe);
    const auto code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return {code, stdout};
}

static auto e2e_run(std::string_view source, std::string_view filepath) noexcept -> std::pair<int, std::string> {
    const auto driver = Driver{};
    const auto result = transpile(driver, source);
    if (!result.errors.empty() || result.output.empty()) return {-1, ""};

    const auto path = build_artifacts(filepath, driver.output_dir);
    ensure_directory(driver.output_dir);
    write_file_if_changed(path.cpp_path, result.output);

    const auto cxx = compiler_from_environment("c++");
    const auto std_flag = std::format("-std=c++{}", driver.language_standard);
    const auto compile_cmd = std::format("{} {} {} -o {}", cxx, std_flag, path.cpp_path.string(), path.exe_path.string());
    const auto [compile_code, _] = run_and_capture(compile_cmd + " 2>&1");
    if (compile_code != 0) return {compile_code, "compile failed"};

    return run_and_capture(path.exe_path.string());
}

static auto e2e_run_fixture(std::string_view name) noexcept -> std::pair<int, std::string> {
    auto source = SourceFile::from_file(name);
    if (!source.has_value()) return {-1, "cannot open fixture"};
    return e2e_run(source->text(), source->filepath());
}

TEST_CASE("E2E: helloworld") {
    const auto [code, stdout] = e2e_run_fixture("tests/fixtures/helloworld.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("Hello World"));
}

static auto transpile_fixture(std::string_view name) noexcept -> bool {
    auto source = SourceFile::from_file(name);
    if (!source.has_value()) return false;
    const auto result = transpile(Driver{}, source->text());
    return result.errors.empty() && !result.output.empty();
}

TEST_CASE("E2E: array") {
    CHECK(transpile_fixture("tests/fixtures/array.cv"));
}

TEST_CASE("E2E: match") {
    const auto [code, stdout] = e2e_run_fixture("tests/fixtures/match.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("two"));
    CHECK(stdout.contains("int"));
}

TEST_CASE("E2E: syntax error") {
    auto source = SourceFile::from_file("tests/fixtures/error.cv");
    REQUIRE(source.has_value());
    const auto driver = Driver{};
    const auto result = transpile(driver, source->text());
    CHECK(!result.errors.empty());
}
