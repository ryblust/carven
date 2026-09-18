module carven:driver.check.impl;

import :driver.analysis;
import :driver.check;
import :semantic.evaluation.output;
import :semantic.semir.program;
import std;

auto run_check_command(std::span<const char* const> args) noexcept -> int {
    if (args.size() == 1
        && (std::string_view(args[0]) == "--help" || std::string_view(args[0]) == "-h")) {
        std::print(
            "Check sources and run required constant evaluation and const tests.\n"
            "\n"
            "Usage:\n"
            "  carven check <source-file>...\n"
            "\n"
            "Options:\n"
            "  -h, --help    Show this help\n"
            "\n"
            "Imports resolve among the supplied sources. No entry point is required.\n"
            "Runtime code is checked without execution; no C++ files are generated.\n"
            "Delegated native operations are checked by the C++ compiler.\n"
        );
        return 0;
    }
    auto paths = std::vector<std::string_view>();
    for (const auto argument : args) {
        const auto arg = std::string_view(argument);
        if (arg.starts_with('-')) {
            std::println(std::cerr, "carven: error: unknown check option '{}'", arg);
            std::println(std::cerr, "Run 'carven check --help' for usage.");
            return 1;
        }
        paths.push_back(arg);
    }
    if (paths.empty()) {
        std::println(std::cerr, "carven: error: check requires at least one source file");
        std::println(std::cerr, "Run 'carven check --help' for usage.");
        return 1;
    }
    const auto program = load_and_analyze_sources(
        paths,
        [](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        }
    );
    return program ? 0 : 1;
}
