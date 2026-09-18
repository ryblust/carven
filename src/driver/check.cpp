module carven:driver.check.impl;

import :driver.analysis;
import :driver.check;
import :driver.timings;
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
            "  carven check [options] <source-file>...\n"
            "\n"
            "Options:\n"
            "      --timings Show total and stage timings on stderr\n"
            "  -h, --help    Show this help\n"
            "\n"
            "Imports resolve among the supplied sources. No entry point is required.\n"
            "Runtime code is checked without execution; no C++ files are generated.\n"
            "Delegated native operations are checked by the C++ compiler.\n"
        );
        return 0;
    }
    auto show_timings = false;
    auto paths = std::vector<std::string_view>();
    for (const auto argument : args) {
        const auto arg = std::string_view(argument);
        if (arg == "--timings") {
            show_timings = true;
            continue;
        }
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
    auto timings = CommandTimings(show_timings, "check");
    const auto program = load_and_analyze_sources(
        paths,
        [](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        },
        timings.recorder()
    );
    if (program) {
        timings.set_outcome("passed");
        if (!show_timings) {
            std::cout.flush();
            std::println(std::cerr, "carven: check passed");
        }
    }
    return program ? 0 : 1;
}
