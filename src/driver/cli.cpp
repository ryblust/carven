module carven:driver.cli.impl;

import :driver.check;
import :driver.cli;
import :driver.compile;
import :driver.dump;
import :driver.interpret;
import :driver.run;
import std;

namespace {

auto print_help() noexcept -> int {
    std::print(
        "Carven compiler and runner\n"
        "\n"
        "Usage:\n"
        "  carven [options] <source-file>... [-- <arguments>...]\n"
        "  carven <command> [options] <source-file>...\n"
        "\n"
        "Commands:\n"
        "  compile      Generate C++ headers and sources\n"
        "  check        Check sources and run const tests\n"
        "  interpret    Run the supported language subset with the interpreter\n"
        "  dump         Inspect tokens or the syntax tree\n"
        "\n"
        "Options:\n"
        "      --tests      Compile and run runtime tests instead of the program entry\n"
        "      --timings    Show total and stage timings on stderr\n"
        "  -h, --help       Show this help\n"
        "  -V, --version    Show the version\n"
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

auto carven_main(int argc, const char* const* argv) noexcept -> int {
    const auto args = std::span(argv + 1, static_cast<std::size_t>(argc - 1));

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
        return run_compile_command(argv[0], args.subspan(1));
    }

    if (first_arg == "check") {
        return run_check_command(argv[0], args.subspan(1));
    }

    if (first_arg == "interpret") {
        return run_interpret_command(argv[0], args.subspan(1));
    }

    return run_native_command(argv[0], args);
}
