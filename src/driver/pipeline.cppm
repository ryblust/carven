export module carven.driver.pipeline;

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

export struct TranspileOptions final {
    std::uint8_t language_standard = 23;
    bool import_std = false;
};

export struct WriteTranspiledSourceConfig final {
    std::string_view source;
    std::string_view filepath;
    std::string_view output_path;
    TranspileOptions options;
};

export struct SingleFileBuildConfig final {
    std::string_view source_file;
    std::string_view carven_executable;
    TranspileOptions options;
};

export struct SingleFileRunConfig final {
    std::string_view source_file;
    std::span<const std::string_view> forwarded_args;
    std::string_view carven_executable;
    TranspileOptions options;
};

struct SingleFileProjectConfig final {
    std::string_view source_file;
    std::string_view command_name;
    std::string_view carven_executable;
    TranspileOptions options = {};
};

auto write_text_file(const std::filesystem::path& path, std::string_view content) noexcept -> bool {
    auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
    return file.is_open() && file.write(content.data(), content.size()).good();
}

auto create_output_parent(const std::filesystem::path& path) noexcept -> bool {
    if (path.has_parent_path()) {
        auto error = std::error_code();
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            std::println("carven transpile: error: cannot create '{}'", path.parent_path().generic_string());
            return false;
        }
    }
    return true;
}

auto absolute_path_text(std::string_view path) noexcept -> std::optional<std::string> {
    auto error = std::error_code();
    const auto absolute = std::filesystem::absolute(std::filesystem::path(path), error);
    if (error) return std::nullopt;
    return absolute.generic_string();
}

auto command_program_path(std::string_view program) noexcept -> std::string {
    const auto program_path = std::filesystem::path(program);
    if (!program_path.has_parent_path()) {
        return std::string(program);
    }

    if (const auto absolute = absolute_path_text(program)) return *absolute;
    return std::string(program);
}

auto write_single_file_project(SingleFileProjectConfig project) noexcept -> std::optional<SingleFileConfig> {
    const auto absolute_source = absolute_path_text(project.source_file);
    if (!absolute_source) {
        std::println("carven {}: error: cannot resolve '{}'", project.command_name, project.source_file);
        return std::nullopt;
    }

    const auto carven_program = command_program_path(project.carven_executable);
    const auto config = make_single_file_config({
        .absolute_source_path = *absolute_source,
        .carven_program = carven_program,
        .standard = project.options.language_standard,
        .import_std = project.options.import_std,
    });
    if (!write_xmake_single_file(config)) {
        std::println("carven {}: error: cannot write temporary xmake project '{}'", project.command_name, config.root_dir);
        return std::nullopt;
    }

    return config;
}

auto run_project_command(std::string_view command_name, const std::vector<std::string>& args) noexcept -> int {
    const auto root = find_project_root(std::filesystem::current_path());
    if (!root) {
        std::println("carven {}: error: cannot find xmake.lua in current directory or parents", command_name);
        return 1;
    }

    const auto exit_code = spawn(args, *root);
    if (exit_code < 0) {
        std::println("carven {}: error: cannot start xmake", command_name);
        return 1;
    }

    return exit_code;
}

export auto transpile(std::string_view source, TranspileOptions options) noexcept -> TranspileResult {
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
        .output = generate(parse_result.items, source, options.language_standard, options.import_std || has_std_module(parse_result.items)),
        .errors = {}
    };
}

export auto print_transpiled_source(std::string_view source, std::string_view filepath, TranspileOptions options) noexcept -> int {
    const auto result = transpile(source, options);
    if (!result.errors.empty()) {
        report_errors(result.errors, source, filepath);
        return 1;
    }

    std::print("{}", result.output);
    return 0;
}

export auto write_transpiled_source(WriteTranspiledSourceConfig request) noexcept -> int {
    const auto result = transpile(request.source, request.options);
    if (!result.errors.empty()) {
        report_errors(result.errors, request.source, request.filepath);
        return 1;
    }

    const auto path = std::filesystem::path(request.output_path);
    if (!create_output_parent(path)) return 1;

    if (!write_text_file(path, result.output)) {
        std::println("carven transpile: error: cannot write '{}'", path.generic_string());
        return 1;
    }

    return 0;
}

export auto build_single_file(SingleFileBuildConfig request) noexcept -> int {
    const auto config = write_single_file_project({
        .source_file = request.source_file,
        .command_name = "build",
        .carven_executable = request.carven_executable,
        .options = request.options,
    });
    if (!config) return 1;

    const auto build_args = xmake_build_args(config->target_name);
    const auto exit_code = spawn(build_args, config->root_dir);
    if (exit_code < 0) {
        std::println("carven build: error: cannot start xmake");
        return 1;
    }
    if (exit_code != 0) {
        std::println("carven build: error: xmake build failed");
    }

    return exit_code;
}

export auto build_project(std::optional<std::string_view> target) noexcept -> int {
    const auto args = xmake_build_args(target.value_or(std::string_view()));
    return run_project_command("build", args);
}

export auto run_single_file(SingleFileRunConfig request) noexcept -> int {
    const auto config = write_single_file_project({
        .source_file = request.source_file,
        .command_name = "run",
        .carven_executable = request.carven_executable,
        .options = request.options,
    });
    if (!config) return 1;

    const auto exit_code = spawn(xmake_run_args(config->target_name, request.forwarded_args), config->root_dir);
    if (exit_code < 0) {
        std::println("carven run: error: cannot start xmake");
        return 1;
    }

    return exit_code;
}

export auto run_project(std::optional<std::string_view> target, std::span<const std::string_view> forwarded_args) noexcept -> int {
    if (!target && !forwarded_args.empty()) {
        std::println("carven run: error: project runtime args require an explicit target");
        return 1;
    }

    const auto args = xmake_run_args(target.value_or(std::string_view()), forwarded_args);
    return run_project_command("run", args);
}

export auto init_project(std::string_view project_path, std::uint8_t language_standard, std::string_view carven_executable) noexcept -> int {
    const auto project_dir = std::filesystem::path(project_path);
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

    static constexpr auto main_source =
        "import std;\n\n"
        "fn main() {\n"
        "    std::println(\"Hello from Carven\");\n"
        "}\n";

    if (!write_text_file(source_dir / "main.cv", main_source)) {
        std::println("carven init: error: cannot write '{}/src/main.cv'", project_dir.generic_string());
        return 1;
    }

    const auto project_name = sanitize_target_name(project_dir.filename().generic_string());
    const auto sources = std::vector<std::string> { "src/main.cv" };

    if (!write_text_file(project_dir / "xmake.lua", generate_xmake_project(project_name, sources, language_standard))) {
        std::println("carven init: error: cannot write '{}/xmake.lua'", project_dir.generic_string());
        return 1;
    }
    if (!write_xmake_carven_rule(project_dir, carven_executable)) {
        std::println("carven init: error: cannot write '{}/xmake/rules/carven.lua'", project_dir.generic_string());
        return 1;
    }

    std::println("created Carven project '{}'", project_dir.generic_string());
    return 0;
}
