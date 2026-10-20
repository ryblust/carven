module carven:driver.cli.impl;

import :driver.cli;
import :driver.compile;
import :driver.diagnostic;
import :driver.dump;
import :driver.interpret;
import :driver.options;
import :support.process;
import std;

namespace {

auto run_project_command(std::string_view command, std::span<const std::string> args) noexcept
    -> int {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        std::println(
            "Usage: carven {}{}\n\nBuild an explicit source file or execute the current directory's build.cv.\n"
            "The standard builder writes outputs under build/.",
            command,
            command == "run" ? " [<source.cv>|<target>] [-- <arguments>...]" : " [<source.cv>]"
        );
        return 0;
    }
    if (!args.empty() && std::string_view(args[0]).ends_with(".cv")) {
        return run_interpret_function("crafts.carven.std.build.cli", "execute_source");
    }
    return run_interpret_command(std::array<std::string, 1> {"build.cv"});
}

auto print_help() noexcept -> int {
    std::print(
        "Carven compiler and runner.\n"
        "\n"
        "Usage:\n"
        "  carven build [<source.cv>]\n"
        "  carven run [<source.cv>|<target>] [-- <arguments>...]\n"
        "  carven <command> [options] <input>...\n"
        "\n"
        "Commands:\n"
        "  build                           Build a source file or execute build.cv\n"
        "  run                             Build and run an executable\n"
        "  compile                         Generate C++ artifacts\n"
        "  check                           Check sources and run const tests\n"
        "  interpret                       Interpret supported programs and tests\n"
        "  dump                            Print tokens or the syntax tree\n"
        "\n"
        "Options:\n"
        "  -h, --help                      Show this help\n"
        "  -V, --version                   Show the version\n"
        "\n"
        "Pass a .cv source to build or run, or use the current directory's build.cv.\n"
        "Use 'carven <command> --help' for command options.\n"
    );
    return 0;
}

auto print_version() noexcept -> int {
    std::println("carven v0.1.0");
    return 0;
}

} // namespace

auto carven_main(int argc, const char* const* native_argv) noexcept -> int {
    const auto arguments = process::arguments(argc, native_argv);
    if (!arguments) {
        return emit_driver_error(arguments.error());
    }
    const auto argv = std::span(*arguments);
    const auto args = argv.subspan(1);

    if (args.empty()) {
        return print_help();
    }

    const auto first_arg = std::string_view(args[0]);
    if (first_arg == "--help" || first_arg == "-h") {
        return print_help();
    } else if (first_arg == "--version" || first_arg == "-V") {
        return print_version();
    } else if (first_arg == "dump") {
        return run_dump_command(args.subspan(1));
    }

    if (first_arg == "build" || first_arg == "run") {
        return run_project_command(first_arg, args.subspan(1));
    }

    if (first_arg == "compile") {
        return run_compilation_command(args.subspan(1), CompilationCommand::Compile);
    }
    if (first_arg == "check") {
        return run_compilation_command(args.subspan(1), CompilationCommand::Check);
    }

    if (first_arg == "interpret") {
        return run_interpret_command(args.subspan(1));
    }

    return emit_driver_error("expected a command; use 'carven --help' for commands", "carven");
}
