export module carven.driver.command.init;

import carven.common.filesystem;
import carven.backend.codegen;
import carven.driver.xmake;
import std;

export struct InitCommand final {
    static constexpr auto name = "init";
    static constexpr auto description = "Create a Carven xmake project";
    static constexpr auto help_message =
        R"(carven init - Create a Carven xmake project

USAGE:
    carven init [options...] <project-dir>

OPTIONS:
    -std=c++<value>    Target C++ standard for generated xmake.lua
)";

    CodegenOptions options;
    std::string_view project_dir;
};

auto init(std::string_view project_path, CodegenOptions options) noexcept -> int {
    const auto project_dir = std::filesystem::path(project_path);
    auto error = std::error_code();
    const auto project_exists = std::filesystem::exists(project_dir, error);

    if (error) {
        std::println("carven init: error: cannot inspect '{}'", project_dir.generic_string());
        return 1;
    }

    const auto project_is_empty = project_exists ? std::filesystem::is_empty(project_dir, error) : true;

    if (error) {
        std::println("carven init: error: cannot inspect '{}'", project_dir.generic_string());
        return 1;
    }

    if (project_exists && !project_is_empty) {
        std::println("carven init: error: '{}' is not empty", project_dir.generic_string());
        return 1;
    }

    const auto source_dir = project_dir / "src";
    std::filesystem::create_directories(source_dir, error);

    if (error) {
        std::println("carven init: error: cannot create '{}'", source_dir.generic_string());
        return 1;
    }

    static constexpr auto main_source = "import std;\n\n"
                                        "fn main() {\n"
                                        "    std::println(\"Hello from Carven\");\n"
                                        "}\n";

    if (!write_file(source_dir / "main.cv", main_source)) {
        std::println("carven init: error: cannot write '{}/src/main.cv'", project_dir.generic_string());
        return 1;
    }

    const auto project_name = sanitize_xmake_target_name(project_dir.filename().generic_string());
    const auto sources = std::vector<std::string> { "src/main.cv" };

    if (!write_file(project_dir / "xmake.lua", generate_xmake_project_file(project_name, sources, options))) {
        std::println("carven init: error: cannot write '{}/xmake.lua'", project_dir.generic_string());
        return 1;
    }

    if (!write_carven_xmake_rule(project_dir)) {
        std::println("carven init: error: cannot write '{}/xmake/rules/carven.lua'", project_dir.generic_string());
        return 1;
    }

    std::println("created Carven project '{}'", project_dir.generic_string());
    return 0;
}

export auto execute(const InitCommand& command) noexcept -> int {
    return init(command.project_dir, command.options);
}
