module carven:driver.cli.impl;

import :driver.cli;
import :driver.compile;
import :driver.diagnostic;
import :driver.dump;
import :driver.interpret;
import :driver.options;
import :driver.run;
import :support.process;
import std;

namespace {

auto print_help() noexcept -> int {
    std::print(
        "Carven compiler and runner.\n"
        "\n"
        "Usage:\n"
        "  carven [options] <source-file>... [-- <arguments>...]\n"
        "  carven <command> [options] <input>...\n"
        "\n"
        "Commands:\n"
        "  compile                         Generate C++ artifacts\n"
        "  check                           Check sources and run const tests\n"
        "  interpret                       Interpret supported programs and tests\n"
        "  dump                            Print tokens or the syntax tree\n"
        "\n"
        "Options:\n"
        "      --tests                     Run runtime tests instead of the program\n"
        "      --timings                   Show total and stage timings on stderr\n"
        "  -h, --help                      Show this help\n"
        "  -V, --version                   Show the version\n"
        "\n"
        "Source files without a command are compiled and run natively.\n"
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

    if (first_arg == "compile") {
        return run_compilation_command(argv[0], args.subspan(1), CompilationCommand::Compile);
    }
    if (first_arg == "check") {
        return run_compilation_command(argv[0], args.subspan(1), CompilationCommand::Check);
    }

    if (first_arg == "interpret") {
        return run_interpret_command(argv[0], args.subspan(1));
    }

    return run_native_command(argv[0], args);
}
