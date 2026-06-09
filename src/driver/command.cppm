export module carven.driver.command;

export import carven.driver.request;
import carven.driver.command.build;
import carven.driver.command.check;
import carven.driver.command.dump;
import carven.driver.command.init;
import carven.driver.command.run;
import carven.driver.command.transpile;
import carven.driver.toolchain;
import std;

export struct Flag final {
    std::string_view long_name;
    std::string_view short_name;
    std::string_view description;
};

export struct Command final {
    std::string_view name;
    std::string_view description;
    std::span<const Flag> flags;
};

constexpr auto GLOBAL_FLAGS = std::array<Flag, 4> {{
    { .long_name = "--help",    .short_name = "-h", .description = "Show help message"          },
    { .long_name = "--version", .short_name = "-V", .description = "Show Carven version"        },
    { .long_name = "--verbose", .short_name = "-v", .description = "Enable verbose diagnostics" },
    { .long_name = "--quiet",   .short_name = "-q", .description = "Suppress non-error output"  },
}};

constexpr auto RUN_FLAGS = std::array<Flag, 2> {{
    { .long_name = "-std=c++<value>", .short_name = "", .description = "Target C++ standard for single-file runs"                             },
    { .long_name = "--import-std",    .short_name = "", .description = "Force #include std headers (auto if source has import std)"          },
}};

constexpr auto DUMP_FLAGS = std::array<Flag, 2> {{
    { .long_name = "--only-tokens", .short_name = "", .description = "Show only token stream" },
    { .long_name = "--only-ast",    .short_name = "", .description = "Show only AST"          },
}};

constexpr auto INIT_FLAGS = std::array<Flag, 1> {{
    { .long_name = "-std=c++<value>", .short_name = "", .description = "Target C++ standard for generated xmake.lua" },
}};

constexpr auto BUILD_FLAGS = std::array<Flag, 2> {{
    { .long_name = "-std=c++<value>", .short_name = "", .description = "Target C++ standard for single-file builds" },
    { .long_name = "--import-std",    .short_name = "", .description = "Force #include std headers for single-file builds" },
}};

constexpr auto TRANSPILE_FLAGS = std::array<Flag, 3> {{
    { .long_name = "-std=c++<value>", .short_name = "", .description = "Target C++ standard for generated output"                 },
    { .long_name = "-o <path>",       .short_name = "", .description = "Write generated C++ to file instead of stdout"            },
    { .long_name = "--import-std",    .short_name = "", .description = "Force #include std headers (auto if source has import std)" },
}};

constexpr auto COMMANDS = std::array<Command, 6> {{
    { .name = "init",      .description = "Create a Carven xmake project",       .flags = INIT_FLAGS      },
    { .name = "run",       .description = "Run a .cv file or xmake target",      .flags = RUN_FLAGS       },
    { .name = "transpile", .description = "Transpile .cv files to C++",          .flags = TRANSPILE_FLAGS },
    { .name = "dump",      .description = "Dump token stream and AST",           .flags = DUMP_FLAGS      },
    { .name = "build",     .description = "Build a .cv file or xmake project",   .flags = BUILD_FLAGS     },
    { .name = "check",     .description = "Parse and check without codegen",     .flags = {}              },
}};

auto command_error(std::string_view command, std::string message) noexcept -> std::unexpected<CommandError> {
    return std::unexpected(CommandError {
        .command = std::string(command),
        .message = std::move(message),
    });
}

auto parse_standard(std::string_view command, std::string_view standard) noexcept -> std::expected<std::uint8_t, CommandError> {
    if      (standard == "c++14") return 14;
    else if (standard == "c++17") return 17;
    else if (standard == "c++20") return 20;
    else if (standard == "c++23") return 23;
    else if (standard == "c++26") return 26;
    return command_error(command, std::format("unknown standard '{}'", standard));
}

auto too_many_args(std::string_view command) noexcept -> std::unexpected<CommandError> {
    return command_error(command, "too many positional arguments");
}

auto parse_init(std::span<const char* const> args) noexcept -> std::expected<InitRequest, CommandError> {
    auto request = InitRequest{};
    auto project_dir = std::optional<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with("-std=")) {
            const auto standard = parse_standard("init", arg.substr(5));
            if (!standard) return std::unexpected(standard.error());
            request.language_standard = *standard;
        } else if (arg.starts_with('-')) {
            return command_error("init", std::format("unknown flag '{}'", arg));
        } else if (project_dir) {
            return too_many_args("init");
        } else {
            project_dir = arg;
        }
    }

    if (!project_dir) return command_error("init", "no project path");
    request.project_dir = *project_dir;
    return request;
}

auto parse_run(std::span<const char* const> args) noexcept -> std::expected<RunRequest, CommandError> {
    auto request = RunRequest{};
    auto positional = std::vector<std::string_view>();
    auto forwarded = std::vector<std::string_view>();
    auto double_dash = false;

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (double_dash) {
            forwarded.push_back(arg);
        } else if (arg == "--") {
            double_dash = true;
        } else if (arg.starts_with("-std=")) {
            const auto standard = parse_standard("run", arg.substr(5));
            if (!standard) return std::unexpected(standard.error());
            request.language_standard = *standard;
        } else if (arg == "--import-std") {
            request.import_std = true;
        } else if (arg.starts_with('-')) {
            return command_error("run", std::format("unknown flag '{}'", arg));
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        if (!forwarded.empty()) return command_error("run", "project runtime args require an explicit target");
        request.mode = ProjectRun { .target = std::nullopt, .args = std::move(forwarded) };
    } else if (positional.size() == 1) {
        if (is_carven_source_path(positional[0])) {
            request.mode = SingleFileRun { .source_file = positional[0], .args = std::move(forwarded) };
        } else {
            request.mode = ProjectRun { .target = positional[0], .args = std::move(forwarded) };
        }
    } else {
        return too_many_args("run");
    }

    return request;
}

auto parse_build(std::span<const char* const> args) noexcept -> std::expected<BuildRequest, CommandError> {
    auto request = BuildRequest{};
    auto positional = std::vector<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with("-std=")) {
            const auto standard = parse_standard("build", arg.substr(5));
            if (!standard) return std::unexpected(standard.error());
            request.language_standard = *standard;
        } else if (arg == "--import-std") {
            request.import_std = true;
        } else if (arg.starts_with('-')) {
            return command_error("build", std::format("unknown flag '{}'", arg));
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        request.mode = ProjectBuild { .target = std::nullopt };
    } else if (positional.size() == 1) {
        if (is_carven_source_path(positional[0])) {
            request.mode = SingleFileBuild { .source_file = positional[0] };
        } else {
            request.mode = ProjectBuild { .target = positional[0] };
        }
    } else {
        return too_many_args("build");
    }

    return request;
}

auto parse_transpile(std::span<const char* const> args) noexcept -> std::expected<TranspileRequest, CommandError> {
    auto request = TranspileRequest{};

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with("-std=")) {
            const auto standard = parse_standard("transpile", arg.substr(5));
            if (!standard) return std::unexpected(standard.error());
            request.language_standard = *standard;
        } else if (arg == "-o") {
            if (i + 1 >= args.size()) return command_error("transpile", "missing output path after '-o'");
            ++i;
            request.output_file = std::string_view(args[i]);
        } else if (arg == "--import-std") {
            request.import_std = true;
        } else if (arg.starts_with('-')) {
            return command_error("transpile", std::format("unknown flag '{}'", arg));
        } else {
            request.source_files.push_back(arg);
        }
    }

    if (request.source_files.empty()) return command_error("transpile", "no input file");
    if (request.output_file && request.source_files.size() != 1) {
        return command_error("transpile", "'-o' requires exactly one input file");
    }
    return request;
}

auto parse_dump(std::span<const char* const> args) noexcept -> std::expected<DumpRequest, CommandError> {
    auto request = DumpRequest{};
    auto source_file = std::optional<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg == "--only-tokens") {
            request.only_tokens = true;
        } else if (arg == "--only-ast") {
            request.only_ast = true;
        } else if (arg.starts_with('-')) {
            return command_error("dump", std::format("unknown flag '{}'", arg));
        } else if (source_file) {
            return too_many_args("dump");
        } else {
            source_file = arg;
        }
    }

    if (!source_file) return command_error("dump", "no input file");
    request.source_file = *source_file;
    return request;
}

auto parse_check(std::span<const char* const> args) noexcept -> std::expected<CheckRequest, CommandError> {
    auto source_file = std::optional<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with('-')) {
            return command_error("check", std::format("unknown flag '{}'", arg));
        } else if (source_file) {
            return too_many_args("check");
        } else {
            source_file = arg;
        }
    }

    if (!source_file) return command_error("check", "no input file");
    return CheckRequest { .source_file = *source_file };
}

auto render_flag(Flag flag) noexcept -> void {
    if (flag.short_name.empty()) {
        std::println("    {:<28}{}", flag.long_name, flag.description);
    } else {
        std::println("    {:<18}{:<10}{}", flag.long_name, flag.short_name, flag.description);
    }
}

export auto render_help() noexcept -> int {
    std::println("Carven Language Toolchain\n");
    std::println("USAGE:\n    carven <command> [options...]\n");
    std::println("COMMANDS:");

    for (const auto& command : COMMANDS) {
        std::println("    {:<18}{}", command.name, command.description);
    }

    std::println("\nGLOBAL OPTIONS:");
    for (const auto flag : GLOBAL_FLAGS) {
        render_flag(flag);
    }

    std::println("\nRun 'carven <command> --help' for more information on a specific command.");
    return 0;
}

export auto render_version() noexcept -> int {
    std::println("carven 0.1.0");
    return 0;
}

export auto render_command_help(Command command) noexcept -> int {
    std::println("carven {} — {}\n", command.name, command.description);
    std::println("USAGE:\n    carven {} [options...]\n", command.name);

    if (command.flags.empty()) {
        std::println("No command-specific options.");
    } else {
        std::println("OPTIONS:");
        for (const auto flag : command.flags) {
            render_flag(flag);
        }
    }
    return 0;
}

export constexpr auto find_command(std::string_view name) noexcept -> std::optional<Command> {
    if (const auto it = std::ranges::find(COMMANDS, name, &Command::name); it != COMMANDS.end()) {
        return *it;
    }
    return std::nullopt;
}

export auto parse_command(std::string_view name, std::span<const char* const> args, std::string_view carven_executable) noexcept -> std::expected<CommandInvocation, CommandError> {
    auto request = std::optional<CommandRequest>();

    if (name == "init") {
        auto parsed = parse_init(args);
        if (!parsed) return std::unexpected(parsed.error());
        request = std::move(*parsed);
    } else if (name == "run") {
        auto parsed = parse_run(args);
        if (!parsed) return std::unexpected(parsed.error());
        request = std::move(*parsed);
    } else if (name == "transpile") {
        auto parsed = parse_transpile(args);
        if (!parsed) return std::unexpected(parsed.error());
        request = std::move(*parsed);
    } else if (name == "dump") {
        auto parsed = parse_dump(args);
        if (!parsed) return std::unexpected(parsed.error());
        request = std::move(*parsed);
    } else if (name == "build") {
        auto parsed = parse_build(args);
        if (!parsed) return std::unexpected(parsed.error());
        request = std::move(*parsed);
    } else if (name == "check") {
        auto parsed = parse_check(args);
        if (!parsed) return std::unexpected(parsed.error());
        request = std::move(*parsed);
    } else {
        return command_error(name, "unknown command");
    }

    return CommandInvocation {
        .carven_executable = carven_executable,
        .request = std::move(*request),
    };
}

export auto render_command_error(const CommandError& error) noexcept -> int {
    std::println("carven {}: error: {}", error.command, error.message);
    return 1;
}

export auto dispatch(const CommandInvocation& invocation) noexcept -> int {
    return std::visit([&](const auto& request) noexcept -> int {
        using Request = std::remove_cvref_t<decltype(request)>;
        if constexpr (std::same_as<Request, InitRequest>) {
            return execute(request, invocation.carven_executable);
        } else if constexpr (std::same_as<Request, RunRequest>) {
            return execute(request, invocation.carven_executable);
        } else if constexpr (std::same_as<Request, BuildRequest>) {
            return execute(request, invocation.carven_executable);
        } else {
            return execute(request);
        }
    }, invocation.request);
}
