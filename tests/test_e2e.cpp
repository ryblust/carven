#include "test_helpers.h"

import carven.common.filesystem;
import carven.common.process;
import carven.driver.toolchain;
import std;

struct E2eProject final {
    std::string root_dir;
    int build_code = -1;
    std::string build_output;
};

static auto carven_program() noexcept -> std::string {
    const auto value = std::getenv("CARVEN_E2E_CARVEN");
    if (value != nullptr && !std::string_view(value).empty()) {
        return std::string(value);
    }

    return "carven";
}

static auto copy_carven_rule(const std::filesystem::path& root) noexcept -> bool {
    const auto rule_dir = root / "xmake" / "rules";
    auto error = std::error_code();
    std::filesystem::create_directories(rule_dir, error);
    if (error) return false;

    std::filesystem::copy_file(
        "xmake/rules/carven.lua",
        rule_dir / "carven.lua",
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    return !error;
}

static auto write_batch_project_files(const std::filesystem::path& root) noexcept -> bool {
    auto error = std::error_code();
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    if (error) return false;
    if (!copy_carven_rule(root)) return false;

    static constexpr auto xmake_lua =
        "set_project(\"carven-e2e-batch\")\n"
        "add_rules(\"mode.debug\", \"mode.release\")\n"
        "set_languages(\"c++23\")\n"
        "set_defaultmode(\"debug\")\n"
        "\n"
        "local carven_program = os.getenv(\"CARVEN_E2E_CARVEN\") or \"carven\"\n"
        "\n"
        "includes(\"xmake/rules/carven.lua\")\n"
        "\n"
        "target(\"hello\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.program\", carven_program)\n"
        "    set_values(\"carven.standard\", \"c++23\")\n"
        "    add_files(\"hello.cv\")\n"
        "\n"
        "target(\"array_runtime\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.program\", carven_program)\n"
        "    set_values(\"carven.standard\", \"c++23\")\n"
        "    add_files(\"array_runtime.cv\")\n"
        "\n"
        "target(\"match_control\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.program\", carven_program)\n"
        "    set_values(\"carven.standard\", \"c++23\")\n"
        "    add_files(\"match_control.cv\")\n"
        "\n"
        "target(\"std_import\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.program\", carven_program)\n"
        "    set_values(\"carven.standard\", \"c++23\")\n"
        "    add_files(\"std_import.cv\")\n"
        "\n"
        "target(\"args_probe\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.program\", carven_program)\n"
        "    set_values(\"carven.standard\", \"c++23\")\n"
        "    add_files(\"args_probe.cv\")\n";

    static constexpr auto hello_source =
        "import std;\n"
        "\n"
        "fn main() {\n"
        "    std::println(\"Hello World\");\n"
        "}\n";

    static constexpr auto array_source =
        "import std;\n"
        "\n"
        "fn sum(data: [i32; 3]) -> i32 {\n"
        "    return data[0];\n"
        "}\n"
        "\n"
        "fn main() {\n"
        "    let a = [1, 2, 3];\n"
        "    let m = [[1, 2], [3, 4]];\n"
        "    let b: [i32; 3] = [1, 2, 3];\n"
        "    std::println(\"array {} {} {}\", sum(b), a[1], m[1][0]);\n"
        "}\n";

    static constexpr auto match_source =
        "import std;\n"
        "\n"
        "fn from_if(x: i32) -> i32 {\n"
        "    if x == 1 { return 7; }\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "fn main() {\n"
        "    let result = match 2 {\n"
        "        1 => { \"one\" }\n"
        "        2 => { \"two\" }\n"
        "        _ => { \"other\" }\n"
        "    };\n"
        "    std::println(\"control {} {}\", from_if(1), result);\n"
        "}\n";

    static constexpr auto std_import_source =
        "import std;\n"
        "\n"
        "fn main() {\n"
        "    std::println(\"std import\");\n"
        "}\n";

    static constexpr auto args_probe_source =
        "import std;\n"
        "\n"
        "fn main(args) {\n"
        "    std::println(\"{}\", std::ranges::distance(args));\n"
        "    let first = *std::ranges::begin(args);\n"
        "    std::println(\"{} {}\", first.first, first.second);\n"
        "}\n";

    return write_file_if_changed(root / "xmake.lua", xmake_lua)
        && write_file_if_changed(root / "hello.cv", hello_source)
        && write_file_if_changed(root / "array_runtime.cv", array_source)
        && write_file_if_changed(root / "match_control.cv", match_source)
        && write_file_if_changed(root / "std_import.cv", std_import_source)
        && write_file_if_changed(root / "args_probe.cv", args_probe_source);
}

static auto build_e2e_project() noexcept -> E2eProject {
    const auto root = std::filesystem::temp_directory_path() / "carven_e2e_batch";
    if (!write_batch_project_files(root)) {
        return {
            .root_dir = root.generic_string(),
            .build_code = -1,
            .build_output = "cannot write e2e batch project",
        };
    }

    const auto args = build_xmake_build_args();
    const auto [code, output] = run_and_capture_in_dir(args, root.generic_string());
    return {
        .root_dir = root.generic_string(),
        .build_code = code,
        .build_output = output,
    };
}

static auto e2e_project() noexcept -> const E2eProject& {
    static const auto project = build_e2e_project();
    return project;
}

static auto run_e2e_target(std::string_view target, std::span<const std::string_view> forwarded_args) noexcept -> std::pair<int, std::string> {
    const auto& project = e2e_project();
    if (project.build_code != 0) {
        return { project.build_code, project.build_output };
    }

    const auto args = build_xmake_run_args(target, forwarded_args);
    return run_and_capture_in_dir(args, project.root_dir);
}

static auto run_e2e_target(std::string_view target) noexcept -> std::pair<int, std::string> {
    const auto forwarded_args = std::array<std::string_view, 0>{};
    return run_e2e_target(target, forwarded_args);
}

TEST_CASE("E2E: batch project builds") {
    const auto& project = e2e_project();
    INFO(project.build_output);
    CHECK_EQ(project.build_code, 0);
}

TEST_CASE("E2E: hello world") {
    const auto [code, output] = run_e2e_target("hello");
    CHECK_EQ(code, 0);
    CHECK(output.contains("Hello World\n"));
}

TEST_CASE("E2E: array runtime behavior") {
    const auto [code, output] = run_e2e_target("array_runtime");
    CHECK_EQ(code, 0);
    CHECK(output.contains("array 1 2 3\n"));
}

TEST_CASE("E2E: match and control flow") {
    const auto [code, output] = run_e2e_target("match_control");
    CHECK_EQ(code, 0);
    CHECK(output.contains("control 7 two\n"));
}

TEST_CASE("E2E: import std lowering path") {
    const auto [code, output] = run_e2e_target("std_import");
    CHECK_EQ(code, 0);
    CHECK(output.contains("std import\n"));
}

TEST_CASE("E2E: target runtime args forwarding") {
    const auto forwarded_args = std::array<std::string_view, 3> { "--name", "Ada", "Lovelace" };
    const auto [code, output] = run_e2e_target("args_probe", forwarded_args);
    CHECK_EQ(code, 0);
    CHECK(output.contains("3\n"));
    CHECK(output.contains("1 --name\n"));
}

TEST_CASE("E2E: carven run single file CLI") {
    const auto source_path = std::filesystem::temp_directory_path() / "carven_e2e_single_file.cv";
    static constexpr auto source =
        "import std;\n"
        "\n"
        "fn main() {\n"
        "    std::println(\"single file\");\n"
        "}\n";

    REQUIRE(write_file_if_changed(source_path, source));

    const auto args = std::vector<std::string> {
        carven_program(),
        "run",
        source_path.generic_string(),
    };
    const auto [code, output] = run_and_capture(args);
    INFO(output);
    CHECK_EQ(code, 0);
    CHECK(output.contains("single file\n"));
}
