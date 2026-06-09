export module carven.driver.pipeline;

import carven.common.filesystem;
import carven.common.process;
import carven.common.source;
import carven.driver.report;
import carven.driver.toolchain;
import carven.frontend.lexer;
import carven.frontend.ast;
import carven.frontend.parser;
import carven.frontend.sema;
import carven.backend.codegen;
import std;

export struct TranspileResult final {
    std::string output;
    std::vector<ParseError> errors;
};

export struct Driver final {
    std::uint8_t language_standard = 23;
    bool import_std = false;
    bool only_tokens = false;
    bool only_ast = false;
    std::string carven_executable = "carven";
    std::optional<std::string_view> output_file;
    std::vector<std::string_view> input_files;
    std::vector<std::string_view> forwarded_args;
};

export constexpr auto parse_flags(std::span<const char* const> flags) noexcept -> std::optional<Driver> {
    auto driver = Driver{};
    auto double_dash = false;

    for (auto i = 0uz; i < flags.size(); ++i) {
        const auto flag = std::string_view(flags[i]);
        if (double_dash) {
            driver.forwarded_args.push_back(flag);
        } else if (flag == "--") {
            double_dash = true;
        } else if (flag.starts_with("-std=")) {
            const auto standard = flag.substr(5);
            if      (standard == "c++14") driver.language_standard = 14;
            else if (standard == "c++17") driver.language_standard = 17;
            else if (standard == "c++20") driver.language_standard = 20;
            else if (standard == "c++23") driver.language_standard = 23;
            else if (standard == "c++26") driver.language_standard = 26;
            else {
                std::println("carven: error: unknown standard '{}'", standard);
                return std::nullopt;
            }
        } else if (flag == "-o") {
            if (i + 1 >= flags.size()) {
                std::println("carven: error: missing output path after '-o'");
                return std::nullopt;
            }
            ++i;
            driver.output_file = std::string_view(flags[i]);
        } else if (flag == "--import-std") {
            driver.import_std = true;
        } else if (flag == "--only-tokens") {
            driver.only_tokens = true;
        } else if (flag == "--only-ast") {
            driver.only_ast = true;
        } else if (flag.starts_with('-')) {
            std::println("carven: error: unknown flag '{}'", flag);
            return std::nullopt;
        } else {
            driver.input_files.push_back(flag);
        }
    }

    return std::optional<Driver>{std::move(driver)};
}

export constexpr auto transpile(std::string_view source, std::uint8_t language_standard, bool import_std) noexcept -> TranspileResult {
    const auto parse_result = parse(tokenize(source), source);

    if (!parse_result.errors.empty()) {
        return {
            .output = {},
            .errors = std::move(parse_result.errors)
        };
    }

    auto sema_errors = analyze(parse_result.items, source);
    if (!sema_errors.empty()) {
        return {
            .output = {},
            .errors = std::move(sema_errors)
        };
    }

    return {
        .output = generate(parse_result.items, source, language_standard, import_std || has_std_module(parse_result.items)),
        .errors = {}
    };
}

auto transpile_single_file(const Driver& driver, std::string_view source, std::string_view filepath) noexcept -> std::optional<std::string> {
    const auto transpile_result = transpile(source, driver.language_standard, driver.import_std);

    if (!transpile_result.errors.empty()) {
        report_errors(transpile_result.errors, source, filepath);
        return std::nullopt;
    }

    return transpile_result.output;
}

export auto print_transpiled_source(const Driver& driver, std::string_view source, std::string_view filepath) noexcept -> int {
    const auto output = transpile_single_file(driver, source, filepath);
    if (!output) return 1;
    std::print("{}", *output);
    return 0;
}

export auto write_transpiled_source(const Driver& driver,
                                    std::string_view source,
                                    std::string_view filepath,
                                    std::string_view output_path) noexcept -> int {
    const auto output = transpile_single_file(driver, source, filepath);
    if (!output) return 1;

    const auto path = std::filesystem::path(output_path);
    const auto parent = path.parent_path();
    if (!parent.empty() && !ensure_directory(parent)) {
        std::println("carven transpile: error: cannot create '{}'", parent.generic_string());
        return 1;
    }

    if (!write_file_if_changed(path, *output)) {
        std::println("carven transpile: error: cannot write '{}'", path.generic_string());
        return 1;
    }

    return 0;
}

auto absolute_path_text(std::string_view path) noexcept -> std::optional<std::string> {
    auto error = std::error_code();
    const auto absolute = std::filesystem::absolute(std::filesystem::path(path), error);
    if (error) return std::nullopt;
    return absolute.generic_string();
}

auto command_program_path(std::string_view program) noexcept -> std::string {
    if (program.find('/') == std::string_view::npos && program.find('\\') == std::string_view::npos) {
        return std::string(program);
    }

    if (const auto absolute = absolute_path_text(program)) return *absolute;
    return std::string(program);
}

auto write_single_file_project(const Driver& driver, std::string_view filepath, std::string_view command_name) noexcept -> std::optional<SingleFileProject> {
    const auto absolute_source = absolute_path_text(filepath);
    if (!absolute_source) {
        std::println("carven {}: error: cannot resolve '{}'", command_name, filepath);
        return std::nullopt;
    }

    const auto project = single_file_project(*absolute_source);
    const auto carven_program = command_program_path(driver.carven_executable);
    if (!write_single_file_xmake(project, *absolute_source, driver.language_standard, carven_program, driver.import_std)) {
        std::println("carven {}: error: cannot write temporary xmake project '{}'", command_name, project.root_dir);
        return std::nullopt;
    }

    return project;
}

export auto build_single_file(const Driver& driver, std::string_view filepath) noexcept -> int {
    const auto project = write_single_file_project(driver, filepath, "build");
    if (!project) return 1;

    const auto build_args = build_xmake_build_args(project->target_name);
    const auto build_result = run_process_in_dir(build_args, project->root_dir);
    if (build_result < 0) {
        std::println("carven build: error: cannot start xmake");
        return 1;
    }
    if (build_result != 0) {
        std::println("carven build: error: xmake build failed");
    }

    return build_result;
}

export auto run_single_file(const Driver& driver, std::string_view filepath) noexcept -> int {
    const auto project = write_single_file_project(driver, filepath, "run");
    if (!project) return 1;

    const auto run_args = build_xmake_run_args(project->target_name, driver.forwarded_args);
    const auto run_result = run_process_in_dir(run_args, project->root_dir);
    if (run_result < 0) {
        std::println("carven run: error: cannot start xmake");
        return 1;
    }

    return run_result;
}

export auto run_project(const Driver& driver) noexcept -> int {
    const auto root = find_project_root(std::filesystem::current_path());
    if (!root) {
        std::println("carven run: error: cannot find xmake.lua in current directory or parents");
        return 1;
    }

    const auto target = driver.input_files.empty() ? std::string_view() : driver.input_files[0];
    if (target.empty() && !driver.forwarded_args.empty()) {
        std::println("carven run: error: project runtime args require an explicit target");
        return 1;
    }

    const auto args = build_xmake_run_args(target, driver.forwarded_args);
    const auto result = run_process_in_dir(args, *root);
    if (result < 0) {
        std::println("carven run: error: cannot start xmake");
        return 1;
    }

    return result;
}

export auto build_project(const Driver& driver) noexcept -> int {
    const auto root = find_project_root(std::filesystem::current_path());
    if (!root) {
        std::println("carven build: error: cannot find xmake.lua in current directory or parents");
        return 1;
    }

    const auto target = driver.input_files.empty() ? std::string_view() : driver.input_files[0];
    const auto args = build_xmake_build_args(target);
    const auto result = run_process_in_dir(args, *root);
    if (result < 0) {
        std::println("carven build: error: cannot start xmake");
        return 1;
    }

    return result;
}

export auto init_project(const Driver& driver) noexcept -> int {
    if (driver.input_files.empty()) {
        std::println("carven init: error: no project path");
        return 1;
    }

    const auto project_dir = std::filesystem::path(driver.input_files[0]);
    auto error = std::error_code();
    if (std::filesystem::exists(project_dir, error) && !std::filesystem::is_empty(project_dir, error)) {
        std::println("carven init: error: '{}' is not empty", project_dir.generic_string());
        return 1;
    }

    const auto source_dir = project_dir / "src";
    std::filesystem::create_directories(source_dir, error);
    if (error) {
        std::println("carven init: error: cannot create '{}'", source_dir.generic_string());
        return 1;
    }

    const auto main_source =
        "import std;\n\n"
        "fn main() {\n"
        "    std::println(\"Hello from Carven\");\n"
        "}\n";
    if (!write_file_if_changed(source_dir / "main.cv", main_source)) {
        std::println("carven init: error: cannot write '{}/src/main.cv'", project_dir.generic_string());
        return 1;
    }

    const auto project_name = sanitize_target_name(project_dir.filename().generic_string());
    const auto sources = std::vector<std::string> { "src/main.cv" };
    if (!write_file_if_changed(project_dir / "xmake.lua", generated_project_xmake(project_name, sources, driver.language_standard))) {
        std::println("carven init: error: cannot write '{}/xmake.lua'", project_dir.generic_string());
        return 1;
    }
    if (!write_carven_rule(project_dir, driver.carven_executable)) {
        std::println("carven init: error: cannot write '{}/xmake/rules/carven.lua'", project_dir.generic_string());
        return 1;
    }

    std::println("created Carven project '{}'", project_dir.generic_string());
    return 0;
}
