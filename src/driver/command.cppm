export module carven.driver.command;

import carven.driver.command.build;
import carven.driver.command.check;
import carven.driver.command.dump;
import carven.driver.command.init;
import carven.driver.command.run;
import carven.driver.command.transpile;
import std;

export auto carven_main(int argc, const char** argv) noexcept -> int;

module :private;

namespace {

auto parse_standard(std::string_view standard) noexcept -> std::expected<std::uint8_t, std::string> {
    if      (standard == "c++14") return 14;
    else if (standard == "c++17") return 17;
    else if (standard == "c++20") return 20;
    else if (standard == "c++23") return 23;
    else if (standard == "c++26") return 26;

    return std::unexpected(std::format("unknown standard {}", standard));
}

auto parse_init(std::span<const char* const> args) noexcept -> std::expected<InitCommand, std::string> {
    auto command = InitCommand{};
    auto project_dir = std::optional<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with("-std=")) {
            const auto standard = parse_standard(arg.substr(5));
            if (!standard) {
                return std::unexpected(standard.error());
            }
            command.options.language_standard = *standard;
        } else if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown flag '{}'", arg));
        } else if (project_dir) {
            return std::unexpected("too many positional arguments");
        } else {
            project_dir = arg;
        }
    }

    if (!project_dir) {
        return std::unexpected("no project path");
    }

    command.project_dir = *project_dir;
    return command;
}

auto parse_run(std::span<const char* const> args) noexcept -> std::expected<RunCommand, std::string> {
    auto command = RunCommand{};
    auto input = std::optional<std::string_view>();
    auto forwarded = std::vector<std::string_view>();
    auto collecting_runtime_args = false;

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (collecting_runtime_args) {
            forwarded.push_back(arg);
        } else if (input) {
            if (arg == "--") {
                collecting_runtime_args = true;
            } else {
                forwarded.push_back(arg);
                collecting_runtime_args = true;
            }
        } else if (arg == "--") {
            collecting_runtime_args = true;
        } else if (arg.starts_with("-std=")) {
            const auto standard = parse_standard(arg.substr(5));
            if (!standard) {
                return std::unexpected(standard.error());
            }
            command.options.language_standard = *standard;
        } else if (arg == "--import-std") {
            command.options.import_std = true;
        } else if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown flag '{}'", arg));
        } else {
            input = arg;
        }
    }

    if (!input && !forwarded.empty()) {
        return std::unexpected("project runtime args require an explicit target");
    }

    command.input = input;
    command.args = std::move(forwarded);
    return command;
}

auto parse_build(std::span<const char* const> args) noexcept -> std::expected<BuildCommand, std::string> {
    auto command = BuildCommand{};
    auto positional = std::vector<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown flag '{}'", arg));
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() > 1) {
        return std::unexpected("too many positional arguments");
    }

    if (!positional.empty()) {
        if (std::filesystem::path(positional[0]).extension() == ".cv") {
            return std::unexpected("single-file builds are not supported; use 'carven run <file>' or 'carven transpile <file>'");
        }
        command.target = positional[0];
    }

    return command;
}

auto parse_transpile(std::span<const char* const> args) noexcept -> std::expected<TranspileCommand, std::string> {
    auto command = TranspileCommand{};

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with("-std=")) {
            const auto standard = parse_standard(arg.substr(5));
            if (!standard) {
                return std::unexpected(standard.error());
            }
            command.options.language_standard = *standard;
        } else if (arg == "-o") {
            if (i + 1 >= args.size()) {
                return std::unexpected("missing output path after '-o'");
            }
            ++i;
            command.output_file = std::string_view(args[i]);
        } else if (arg == "--import-std") {
            command.options.import_std = true;
        } else if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown flag '{}'", arg));
        } else {
            command.source_files.push_back(arg);
        }
    }

    if (command.source_files.empty()) {
        return std::unexpected("no input file");
    }

    if (command.output_file && command.source_files.size() != 1) {
        return std::unexpected("'-o' requires exactly one input file");
    }

    return command;
}

auto parse_dump(std::span<const char* const> args) noexcept -> std::expected<DumpCommand, std::string> {
    auto command = DumpCommand{};
    auto source_file = std::optional<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg == "--only-tokens") {
            command.only_tokens = true;
        } else if (arg == "--only-ast") {
            command.only_ast = true;
        } else if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown flag '{}'", arg));
        } else if (source_file) {
            return std::unexpected("too many positional arguments");
        } else {
            source_file = arg;
        }
    }

    if (!source_file) {
        return std::unexpected("no input file");
    }

    command.source_file = *source_file;
    return command;
}

auto parse_check(std::span<const char* const> args) noexcept -> std::expected<CheckCommand, std::string> {
    auto source_file = std::optional<std::string_view>();

    for (auto i = 0uz; i < args.size(); ++i) {
        const auto arg = std::string_view(args[i]);
        if (arg.starts_with('-')) {
            return std::unexpected(std::format("unknown flag '{}'", arg));
        } else if (source_file) {
            return std::unexpected("too many positional arguments");
        } else {
            source_file = arg;
        }
    }

    if (!source_file) {
        return std::unexpected("no input file");
    }

    return CheckCommand { .source_file = *source_file };
}

auto is_command(std::string_view name) noexcept -> bool {
    return name == InitCommand::name
        || name == RunCommand::name
        || name == TranspileCommand::name
        || name == DumpCommand::name
        || name == BuildCommand::name
        || name == CheckCommand::name;
}

auto render_help() noexcept -> int {
    std::println(
        "Carven Language Toolchain\n"
        "\n"
        "USAGE:\n    carven <command> [options...]\n"
        "\n"
        "COMMANDS:\n"
        "    {:<18}{}\n"
        "    {:<18}{}\n"
        "    {:<18}{}\n"
        "    {:<18}{}\n"
        "    {:<18}{}\n"
        "    {:<18}{}\n"
        "\n"
        "GLOBAL OPTIONS:\n"
        "    {:<18}{:<10}{}\n"
        "    {:<18}{:<10}{}\n"
        "    {:<18}{:<10}{}\n"
        "    {:<18}{:<10}{}\n"
        "\n"
        "Run 'carven <command> --help' for more information on a specific command.",
        InitCommand::name, InitCommand::description,
        RunCommand::name, RunCommand::description,
        TranspileCommand::name, TranspileCommand::description,
        DumpCommand::name, DumpCommand::description,
        BuildCommand::name, BuildCommand::description,
        CheckCommand::name, CheckCommand::description,
        "--help", "-h", "Show help message",
        "--version", "-V", "Show Carven version",
        "--verbose", "-v", "Enable verbose diagnostics",
        "--quiet", "-q", "Suppress non-error output"
    );
    return 0;
}

auto render_version() noexcept -> int {
    std::println("carven 0.1.0");
    return 0;
}

auto render_command_help(std::string_view name) noexcept -> int {
    if (name == InitCommand::name) {
        std::print(InitCommand::help_message);
    } else if (name == RunCommand::name) {
        std::print(RunCommand::help_message);
    } else if (name == TranspileCommand::name) {
        std::print(TranspileCommand::help_message);
    } else if (name == DumpCommand::name) {
        std::print(DumpCommand::help_message);
    } else if (name == BuildCommand::name) {
        std::print(BuildCommand::help_message);
    } else if (name == CheckCommand::name) {
        std::print(CheckCommand::help_message);
    } else {
        std::println("carven {}: error: unknown command", name);
        return 1;
    }

    return 0;
}

using Command = std::variant<InitCommand, RunCommand, BuildCommand, TranspileCommand, DumpCommand, CheckCommand>;

auto parse_command(std::string_view name, std::span<const char* const> args) noexcept -> std::expected<Command, std::string> {
    if (name == InitCommand::name) {
        return parse_init(args);
    } else if (name == RunCommand::name) {
        return parse_run(args);
    } else if (name == TranspileCommand::name) {
        return parse_transpile(args);
    } else if (name == DumpCommand::name) {
        return parse_dump(args);
    } else if (name == BuildCommand::name) {
        return parse_build(args);
    } else if (name == CheckCommand::name) {
        return parse_check(args);
    }
    return std::unexpected(std::format("unknown command '{}'", name));
}

auto dispatch(const Command& command) noexcept -> int {
    return std::visit([](const auto& parsed_command) static noexcept -> int { return execute(parsed_command); }, command);
}

auto run_carven_main(int argc, const char** argv) noexcept -> int {
    const auto args = argc <= 1
        ? std::span<const char* const>()
        : std::span<const char* const>(argv + 1, static_cast<std::size_t>(argc - 1));

    if (args.empty()) {
        return render_help();
    }

    const auto name = std::string_view(args[0]);

    if (name == "--help" || name == "-h") {
        return render_help();
    }

    if (name == "--version" || name == "-V") {
        return render_version();
    }

    if (!is_command(name)) {
        std::println("carven: error: unknown command '{}'", name);
        std::println("Run 'carven --help' for usage information.");
        return 1;
    }

    if (args.size() > 1) {
        const auto flag = std::string_view(args[1]);

        if (flag == "--help" || flag == "-h") {
            return render_command_help(name);
        }
    }

    const auto command = parse_command(name, args.subspan(1));

    if (!command) {
        std::println("carven {}: error: {}", name, command.error());
        return 1;
    }

    return dispatch(*command);
}

}

auto carven_main(int argc, const char** argv) noexcept -> int {
    return run_carven_main(argc, argv);
}
