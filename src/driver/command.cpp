module zero.driver.command;

import zero.driver.handler;
import std;

namespace {

constexpr auto GLOBAL_FLAGS = std::array<Flag, 4> {{
    { .long_name = "--help",    .short_name = "-h", .description = "Show help message"          },
    { .long_name = "--version", .short_name = "-V", .description = "Show Zero version"          },
    { .long_name = "--verbose", .short_name = "-v", .description = "Enable verbose diagnostics" },
    { .long_name = "--quiet",   .short_name = "-q", .description = "Suppress non-error output"  },
}};

constexpr auto RUN_FLAGS = std::array<Flag, 3> {{
    { .long_name = "-std=c++<value>",          .short_name = "", .description = "Target C++ standard (14, 17, 20, 23, 26)" },
    { .long_name = "--output-dir=<path>",      .short_name = "", .description = "Build cache output directory"             },
    { .long_name = "--no-default-include-std", .short_name = "", .description = "Disable auto #include of std headers"     },
}};

constexpr auto DUMP_FLAGS = std::array<Flag, 2> {{
    { .long_name = "--only-tokens", .short_name = "", .description = "Show only token stream" },
    { .long_name = "--only-ast",    .short_name = "", .description = "Show only AST"         },
}};

constexpr auto COMMANDS = std::array<Command, 4> {{
    { .name = "run",   .description = "Transpile and run a .zero file",  .flags = RUN_FLAGS,   .handler = run   },
    { .name = "dump",  .description = "Dump token stream and AST",       .flags = DUMP_FLAGS,  .handler = dump  },
    { .name = "build", .description = "Build a Zero project",            .flags = {},           .handler = build },
    { .name = "check", .description = "Parse and check without codegen", .flags = {},           .handler = check },
}};

auto render_flag(Flag flag) noexcept -> void {
    if (flag.short_name.empty()) {
        std::println("    {:<28}{}", flag.long_name, flag.description);
    } else {
        std::println("    {:<18}{:<10}{}", flag.long_name, flag.short_name, flag.description);
    }
}

} // namespace

auto render_help() noexcept -> int {
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
    return 0;
}

auto render_version() noexcept -> int {
    std::println("zero 0.1.0");
    return 0;
}

auto render_command_help(Command command) noexcept -> int {
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
    return 0;
}

auto find_command(std::string_view name) noexcept -> std::optional<Command> {
    if (const auto it = std::ranges::find(COMMANDS, name, &Command::name); it != COMMANDS.end()) {
        return *it;
    }
    return std::nullopt;
}
