module carven:driver.check.impl;

import :driver.analysis;
import :driver.check;
import :driver.diagnostic;
import :driver.sources;
import :driver.timings;
import :semantic.evaluation.output;
import :semantic.semir.program;
import std;

auto run_check_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int {
    if (args.size() == 1
        && (std::string_view(args[0]) == "--help" || std::string_view(args[0]) == "-h")) {
        std::print(
            "Check sources and run compile-time execution and const tests.\n"
            "\n"
            "Usage:\n"
            "  carven check [options] <source-file>...\n"
            "\n"
            "Options:\n"
            "      --explicit-inputs Use exactly the supplied source files\n"
            "      --source=<module>=<path> Supply a mapped source (repeatable)\n"
            "      --timings         Show total and stage timings on stderr\n"
            "  -h, --help            Show this help\n"
            "\n"
            "Sources include the fixed toolchain and working-directory Crafts roots.\n"
            "No entry point is required.\n"
            "Runtime code is checked without execution; no C++ files are generated.\n"
            "Validation of delegated native operations requires a separate C++ build.\n"
        );
        return 0;
    }
    auto show_timings = false;
    auto explicit_inputs = false;
    auto paths = std::vector<std::string_view>();
    auto mapped_inputs = std::vector<SourceInput>();
    for (const auto argument : args) {
        const auto arg = std::string_view(argument);
        if (arg == "--timings") {
            show_timings = true;
            continue;
        }
        if (arg == "--explicit-inputs") {
            explicit_inputs = true;
            continue;
        }
        if (arg.starts_with("--source=")) {
            auto input = parse_source_input(arg.substr(std::string_view("--source=").size()));
            if (!input) {
                return emit_driver_error(input.error(), "carven check");
            }
            mapped_inputs.push_back(std::move(*input));
            continue;
        }
        if (arg.starts_with('-')) {
            return emit_driver_error(std::format("unknown check option '{}'", arg), "carven check");
        }
        paths.push_back(arg);
    }
    if (paths.empty() && mapped_inputs.empty()) {
        return emit_driver_error("check requires at least one source file", "carven check");
    }
    auto timings = CommandTimings(show_timings, "check");
    const auto sources = collect_command_sources(
        executable,
        paths,
        timings.output(),
        explicit_inputs,
        mapped_inputs
    );
    if (!sources) {
        return emit_driver_error(sources.error());
    }
    const auto program = load_and_analyze_sources(
        sources->carven,
        [](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        },
        timings.output()
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
