export module zero.driver.cli;

import zero.common.file;
import zero.common.source;
import zero.driver.pipeline;
import zero.frontend.lexer;
import zero.frontend.parser;
import std;

struct Flag final {
    std::string_view long_name;
    std::string_view short_name;
    std::string_view description;
};

struct Command final {
    std::string_view name;
    std::string_view description;
    std::span<const Flag> flags;
    auto (*handler)(std::span<const char* const> args, const Driver& driver) -> int;
};

constexpr auto GLOBAL_FLAGS = std::array {
    Flag { .long_name = "--help",    .short_name = "-h", .description = "Show help message"          },
    Flag { .long_name = "--version", .short_name = "-V", .description = "Show Zero version"          },
    Flag { .long_name = "--verbose", .short_name = "-v", .description = "Enable verbose diagnostics" },
    Flag { .long_name = "--quiet",   .short_name = "-q", .description = "Suppress non-error output"  },
};

auto run(std::span<const char* const> args, const Driver& driver) -> int {
    if (args.empty()) {
        std::println("zero run: error: no input file");
        return 1;
    }
    const auto filename = std::string_view(args[0]);
    const auto content = read_file(std::filesystem::path(filename));
    if (!content) {
        std::println("zero run: error: cannot read '{}'", filename);
        return 1;
    }
    return driver.run_single_file(SourceFile{ .filename = std::string(filename), .content = std::move(*content) });
}

auto tokens(std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    if (args.empty()) {
        std::println("zero tokens: error: no input file");
        return 1;
    }
    const auto filename = std::string_view(args[0]);
    const auto content = read_file(std::filesystem::path(filename));
    if (!content) {
        std::println("zero tokens: error: cannot read '{}'", filename);
        return 1;
    }

    const auto tokens = tokenize(*content);
    for (const auto& token : tokens) {
        std::println("{}", token);
    }

    return 0;
}

auto ast(std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    if (args.empty()) {
        std::println("zero ast: error: no input file");
        return 1;
    }
    const auto filename = std::string_view(args[0]);
    const auto content = read_file(std::filesystem::path(filename));
    if (!content) {
        std::println("zero ast: error: cannot read '{}'", filename);
        return 1;
    }

    const auto source = SourceFile{ .filename = std::string(filename), .content = std::move(*content) };
    const auto tokens = tokenize(source.content);
    const auto parse_result = parse(tokens, source.content);
    if (parse_result.has_errors()) {
        for (const auto& error : parse_result.errors) {
            std::println("{}", format_parse_error(error));
        }
        return 1;
    }

    for (const auto& item : parse_result.items) {
        std::visit([&]<typename T>(const T& it) {
            if constexpr (std::is_same_v<T, ImportItem>) {
                std::println("ImportItem");
                std::println("  module: {}", text_at(source.content,it.module_name));
                if (!it.using_decls.empty()) {
                    std::println("  using:");
                    for (const auto& decl : it.using_decls) {
                        std::println("    - {}", text_at(source.content,decl));
                    }
                }
            } else if constexpr (std::is_same_v<T, EnumItem>) {
                std::println("EnumItem");
                std::println("  name: {}", text_at(source.content,it.name));
                for (const auto& field : it.fields) {
                    std::println("    - {}", text_at(source.content,field));
                }
            } else if constexpr (std::is_same_v<T, StructItem>) {
                std::println("StructItem");
                std::println("  name: {}", text_at(source.content,it.name));
                for (const auto& field : it.fields) {
                    std::println("    - {}: {}", text_at(source.content,field.name), text_at(source.content,field.type));
                }
            } else if constexpr (std::is_same_v<T, FunctionItem>) {
                std::println("FunctionItem");
                std::println("  name: {}", text_at(source.content,it.name));
                if (!it.params.empty()) {
                    std::println("  params:");
                    for (const auto& p : it.params) {
                        std::println("    - {}: {}", text_at(source.content,p.name), text_at(source.content,p.type));
                    }
                }
                if (it.return_type.start != 0 || it.return_type.end != 0) {
                    std::println("  returns: {}", text_at(source.content,it.return_type));
                }
            }
        }, item);
    }

    return 0;
}

auto build([[maybe_unused]] std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    std::println("zero build: not yet implemented");
    return 0;
}

auto check(std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    if (args.empty()) {
        std::println("zero check: error: no input file");
        return 1;
    }
    const auto filename = std::string_view(args[0]);
    const auto content = read_file(std::filesystem::path(filename));
    if (!content) {
        std::println("zero check: error: cannot read '{}'", filename);
        return 1;
    }

    const auto tokens = tokenize(*content);
    const auto parse_result = parse(tokens, *content);
    if (parse_result.has_errors()) {
        for (const auto& error : parse_result.errors) {
            std::println("{}", format_parse_error(error));
        }
        return 1;
    }

    std::println("zero check: ok");
    return 0;
}

constexpr auto RUN_FLAGS = std::array {
    Flag { .long_name = "-std=c++<value>",          .short_name = "", .description = "Target C++ standard (14, 17, 20, 23, 26)" },
    Flag { .long_name = "--output-dir=<path>",      .short_name = "", .description = "Build cache output directory"             },
    Flag { .long_name = "--no-default-include-std", .short_name = "", .description = "Disable auto #include of std headers"     },
};

constexpr auto COMMANDS = std::array {
    Command { .name = "run",    .description = "Transpile and run a .zero file",       .flags = RUN_FLAGS, .handler = run },
    Command { .name = "tokens", .description = "Lex a file and dump the token stream", .flags = {}, .handler = tokens },
    Command { .name = "ast",    .description = "Parse a file and dump the AST",        .flags = {}, .handler = ast    },
    Command { .name = "build",  .description = "Build a Zero project",                 .flags = {}, .handler = build  },
    Command { .name = "check",  .description = "Parse and check without codegen",      .flags = {}, .handler = check  },
};

auto render_flag(Flag flag) noexcept -> void {
    if (flag.short_name.empty()) {
        std::println("    {:<28}{}", flag.long_name, flag.description);
    } else {
        std::println("    {:<18}{:<10}{}", flag.long_name, flag.short_name, flag.description);
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
            const auto raw_args = std::span<const char* const>(argv + 2, static_cast<std::size_t>(argc - 2));

            for (const auto flag : raw_args) {
                if (std::string_view(flag) == "--help" || std::string_view(flag) == "-h") {
                    render_command_help(command);
                    return 0;
                }
            }

            auto driver = Driver();
            auto clean_args = std::vector<const char*>();
            clean_args.reserve(raw_args.size());

            for (auto i = 0uz; i < raw_args.size(); ++i) {
                const auto sv = std::string_view(raw_args[i]);
                if (sv.starts_with("-std=")) {
                    const auto val = sv.substr(5);
                    if (val == "c++14")      driver.language_standard = 14;
                    else if (val == "c++17") driver.language_standard = 17;
                    else if (val == "c++20") driver.language_standard = 20;
                    else if (val == "c++23") driver.language_standard = 23;
                    else if (val == "c++26") driver.language_standard = 26;
                    else {
                        std::println("zero: error: unknown standard '{}'", val);
                        return 1;
                    }
                } else if (sv.starts_with("--output-dir=")) {
                    driver.output_dir = sv.substr(13);
                } else if (sv == "--no-default-include-std") {
                    driver.default_include_std = false;
                } else {
                    clean_args.push_back(raw_args[i]);
                }
            }

            return command.handler(clean_args, driver);
        }
    }

    std::println("zero: error: unknown command '{}'", arg);
    std::println("Run 'zero --help' for usage information.");

    return 0;
}
