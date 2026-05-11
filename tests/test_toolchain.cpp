import zero.driver.toolchain;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto test_display_command_simple() noexcept -> void {
    current_test = "display_command simple";
    CHECK_EQ(display_command({"g++", "-std=c++23", "file.cpp"}), "g++ -std=c++23 file.cpp");
}

static auto test_display_command_spaces() noexcept -> void {
    current_test = "display_command with spaces";
    CHECK_EQ(display_command({"g++", "path with spaces.cpp"}), "g++ \"path with spaces.cpp\"");
}

static auto test_display_command_empty() noexcept -> void {
    current_test = "display_command empty";
    CHECK_EQ(display_command({}), "");
}

static auto test_compiler_from_environment_fallback() noexcept -> void {
    current_test = "compiler_from_environment fallback";
    CHECK(!compiler_from_environment("my-fallback").empty());
}

auto main() noexcept -> int {
    test_display_command_simple();
    test_display_command_spaces();
    test_display_command_empty();
    test_compiler_from_environment_fallback();

    return print_test_result();
}
