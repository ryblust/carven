export module zero.driver.cli;

import zero.driver.pipeline;
import zero.frontend.lexer;
import zero.frontend.parser;
import zero.common.source;
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
    auto (*handler)(std::span<const char* const> args, TranspileOptions opts) -> int;
};

constexpr auto GLOBAL_FLAGS = std::array {
    Flag { .long_name = "--help",    .short_name = "-h", .description = "Show help message"          },
    Flag { .long_name = "--version", .short_name = "-V", .description = "Show Zero version"          },
    Flag { .long_name = "--verbose", .short_name = "-v", .description = "Enable verbose diagnostics" },
    Flag { .long_name = "--quiet",   .short_name = "-q", .description = "Suppress non-error output"  },
};

auto read_file(std::string_view path) noexcept -> std::optional<std::string> {
    auto file = std::ifstream(std::string(path), std::ios::ate | std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    const auto size = file.tellg();
    file.seekg(0);

    auto content = std::string(static_cast<std::size_t>(size), '\0');
    file.read(content.data(), size);

    return content;
}

auto run(std::span<const char* const> args, TranspileOptions opts) -> int {
    if (args.empty()) {
        std::println("zero run: error: no input file");
        return 0;
    }

    const auto filename = std::string_view(args[0]);
    const auto content = read_file(filename);
    if (!content) {
        std::println("zero run: error: cannot read '{}'", filename);
        return 0;
    }

    const auto output = transpile(*content, opts);
    std::print("{}", output);

    return 0;
}

auto tokens(std::span<const char* const> args, [[maybe_unused]] TranspileOptions opts) -> int {
    if (args.empty()) {
        std::println("zero tokens: error: no input file");
        return 0;
    }

    const auto filename = std::string_view(args[0]);
    const auto content = read_file(filename);
    if (!content) {
        std::println("zero tokens: error: cannot read '{}'", filename);
        return 0;
    }

    const auto tokens = tokenize(*content);
    for (const auto& token : tokens) {
        std::println("{}", token);
    }

    return 0;
}

auto ast(std::span<const char* const> args, [[maybe_unused]] TranspileOptions opts) -> int {
    if (args.empty()) {
        std::println("zero ast: error: no input file");
        return 0;
    }

    const auto filename = std::string_view(args[0]);
    const auto content = read_file(filename);
    if (!content) {
        std::println("zero ast: error: cannot read '{}'", filename);
        return 0;
    }

    const auto source = SourceFile { .filename = filename, .content = *content };
    const auto tokens = tokenize(source.content);
    const auto items  = parse(tokens, source.content);

    for (const auto& item : items) {
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

auto build([[maybe_unused]] std::span<const char* const> args, [[maybe_unused]] TranspileOptions opts) -> int {
    std::println("zero build: not yet implemented");
    return 0;
}

auto check([[maybe_unused]] std::span<const char* const> args, [[maybe_unused]] TranspileOptions opts) -> int {
    std::println("zero check: not yet implemented");
    return 0;
}

constexpr auto RUN_FLAGS = std::array {
    Flag { .long_name = "-std=c++XX",               .short_name = "", .description = "Target C++ standard (14, 17, 20, 23, 26). Default: c++20" },
    Flag { .long_name = "--no-default-include-std", .short_name = "", .description = "Disable auto #include of all std headers (C++17-)"        },
    Flag { .long_name = "--no-default-import-std",  .short_name = "", .description = "Disable auto import std; (C++20+)"                        },
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

} // namespace

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

            auto opts = TranspileOptions {};
            auto clean_args = std::vector<const char*>();
            clean_args.reserve(raw_args.size());

            for (const auto a : raw_args) {
                const auto sv = std::string_view(a);
                if (sv.starts_with("-std=")) {
                    const auto val = sv.substr(5);
                    if      (val == "c++14") opts.standard = CppStandard::Cpp14;
                    else if (val == "c++17") opts.standard = CppStandard::Cpp17;
                    else if (val == "c++20") opts.standard = CppStandard::Cpp20;
                    else if (val == "c++23") opts.standard = CppStandard::Cpp23;
                    else if (val == "c++26") opts.standard = CppStandard::Cpp26;
                    else {
                        std::println("zero: error: unknown standard '{}'", val);
                        return 0;
                    }
                } else if (sv == "--no-default-include-std") {
                    opts.default_include_std = false;
                } else if (sv == "--no-default-import-std") {
                    opts.default_import_std = false;
                } else {
                    clean_args.push_back(a);
                }
            }

            return command.handler(clean_args, opts);
        }
    }

    std::println("zero: error: unknown command '{}'", arg);
    std::println("Run 'zero --help' for usage information.");

    return 0;
}
