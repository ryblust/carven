export module zero.driver.cli;

import std;

namespace {

struct Flag final {
    std::string_view long_name;
    std::string_view short_name;
    std::string_view description;
};

struct Command final {
    std::string_view name;
    std::string_view description;
    std::span<const Flag> flags;
    auto (*handler)(std::span<const char* const> args) -> int;
};

constexpr auto GLOBAL_FLAGS = std::array {
    Flag{ .long_name = "--help",    .short_name = "-h", .description = "Show help message"          },
    Flag{ .long_name = "--version", .short_name = "-V", .description = "Show Zero version"          },
    Flag{ .long_name = "--verbose", .short_name = "-v", .description = "Enable verbose diagnostics" },
    Flag{ .long_name = "--quiet",   .short_name = "-q", .description = "Suppress non-error output"  },
};

auto cmd_build(std::span<const char* const> args) -> int;
auto cmd_run(std::span<const char* const> args) -> int;
auto cmd_check(std::span<const char* const> args) -> int;
auto cmd_tokens(std::span<const char* const> args) -> int;
auto cmd_ast(std::span<const char* const> args) -> int;

constexpr auto COMMANDS = std::array {
    Command{ .name = "build",  .description = "Build current Zero project",           .flags = {}, .handler = cmd_build  },
    Command{ .name = "run",    .description = "Build and run a single file",          .flags = {}, .handler = cmd_run    },
    Command{ .name = "check",  .description = "Parse and type-check without codegen", .flags = {}, .handler = cmd_check  },
    Command{ .name = "tokens", .description = "Dump lexer tokens",                    .flags = {}, .handler = cmd_tokens },
    Command{ .name = "ast",    .description = "Dump parsed AST",                      .flags = {}, .handler = cmd_ast    },
};

auto render_flag(Flag flag) noexcept -> void {
    if (flag.short_name.empty()) {
        std::println("    {:<18}{}", flag.long_name, flag.description);
    } else {
        std::println("    {:<12}{:<6}{}", flag.long_name, flag.short_name, flag.description);
    }
}

auto render_help() noexcept -> void {
    std::println("Zero Language Toolchain\n");
    std::println("USAGE:\n    zero <command> [options...]\n");
    std::println("COMMANDS:");

    for (const auto& command : COMMANDS) {
        std::println("    {:<18}{}", command.name, command.description);
    }

    std::println("\nGLOBAL OPTIONS:");
    for (const auto flag : GLOBAL_FLAGS) {
        render_flag(flag);
    }

    std::println("\nRun 'zero <command> --help' for more information on a specific command.");
}

auto render_command_help(const Command& command) noexcept -> void {
    std::println("zero {} — {}\n", command.name, command.description);
    std::println("USAGE:\n    zero {} [options...]\n", command.name);

    if (command.flags.empty()) {
        std::println("No command-specific options.");
    } else {
        std::println("OPTIONS:");
        for (const auto flag : command.flags) {
            render_flag(flag);
        }
    }
}

auto cmd_build(std::span<const char* const> /*args*/) -> int {
    std::println("zero build: not yet implemented");
    return 0;
}

auto cmd_run(std::span<const char* const> /*args*/) -> int {
    std::println("zero run: not yet implemented");
    return 0;
}

auto cmd_check(std::span<const char* const> /*args*/) -> int {
    std::println("zero check: not yet implemented");
    return 0;
}

auto cmd_tokens(std::span<const char* const> /*args*/) -> int {
    std::println("zero tokens: not yet implemented");
    return 0;
}

auto cmd_ast(std::span<const char* const> /*args*/) -> int {
    std::println("zero ast: not yet implemented");
    return 0;
}

}

export auto __zero_main__(int argc, char** argv) noexcept -> int {
    if (argc == 1) {
        std::println("zero: error: no input files");
        std::println("Run 'zero --help' for usage information.");

        return 0;
    }

    const auto arg = std::string_view(argv[1]);

    if (arg == "--help" || arg == "-h") {
        render_help();
        return 0;
    }

    if (arg == "--version" || arg == "-V") {
        std::println("zero 0.1.0");
        return 0;
    }

    for (const auto& command : COMMANDS) {
        if (arg == command.name) {
            auto flags = std::span<const char* const>(argv + 2, static_cast<std::size_t>(argc - 2));

            for (const auto flag : flags) {
                if (std::string_view(flag) == "--help" || std::string_view(flag) == "-h") {
                    render_command_help(command);
                    return 0;
                }
            }

            return command.handler(flags);
        }
    }

    std::println("zero: error: unknown command '{}'", arg);
    std::println("Run 'zero --help' for usage information.");

    return 0;
}