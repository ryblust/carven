export module carven.driver.command;

import carven.driver.dump;
import carven.driver.handler;
import carven.driver.pipeline;
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
    auto (*handler)(const Driver& driver) -> int;
};

constexpr auto GLOBAL_FLAGS = std::array<Flag, 4> {{
    { .long_name = "--help",    .short_name = "-h", .description = "Show help message"          },
    { .long_name = "--version", .short_name = "-V", .description = "Show Carven version"        },
    { .long_name = "--verbose", .short_name = "-v", .description = "Enable verbose diagnostics" },
    { .long_name = "--quiet",   .short_name = "-q", .description = "Suppress non-error output"  },
}};

constexpr auto RUN_FLAGS = std::array<Flag, 4> {{
    { .long_name = "-std=c++<value>",          .short_name = "", .description = "Target C++ standard (14, 17, 20, 23, 26)"                   },
    { .long_name = "--output-dir=<path>",      .short_name = "", .description = "Build cache output directory"                               },
    { .long_name = "--import-std",             .short_name = "", .description = "Force #include std headers (auto if source has import std)" },
    { .long_name = "-E",                       .short_name = "", .description = "Only transpile (emit C++, skip compile)"                    },
}};

constexpr auto DUMP_FLAGS = std::array<Flag, 2> {{
    { .long_name = "--only-tokens", .short_name = "", .description = "Show only token stream" },
    { .long_name = "--only-ast",    .short_name = "", .description = "Show only AST"          },
}};

constexpr auto COMMANDS = std::array<Command, 4> {{
    { .name = "run",   .description = "Transpile and run a .cv file",    .flags = RUN_FLAGS,   .handler = run   },
    { .name = "dump",  .description = "Dump token stream and AST",       .flags = DUMP_FLAGS,  .handler = dump  },
    { .name = "build", .description = "Build a Carven project",          .flags = {},          .handler = build },
    { .name = "check", .description = "Parse and check without codegen", .flags = {},          .handler = check },
}};

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
