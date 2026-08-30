module carven:driver.cli.impl;

import :driver.cli;
import :driver.compile;
import :driver.dump;
import std;

namespace {

auto print_help() noexcept -> int {
    std::print(
        "Carven Source-to-C++ Compiler\n"
        "\n"
        "USAGE:\n"
        "    carven [options...] <source-file>...\n"
        "    carven dump tokens <source-file>\n"
        "    carven dump ast <source-file>\n"
        "\n"
        "OPTIONS:\n"
        "    -o, --output-dir <dir>   Write generated files below this directory\n"
        "                             (default: current directory)\n"
        "    --stdout                 Print all generated artifacts for inspection\n"
        "    --tests=default          Emit inline tests and the default runner\n"
        "    --tests=external         Emit inline tests without a runner\n"
        "    --linkage-domain <value> Override the generated linkage domain\n"
        "\n"
        "DEVELOPER COMMANDS:\n"
        "    dump tokens              Dump the token stream to stdout\n"
        "    dump ast                 Dump the syntax tree to stdout\n"
        "\n"
        "GLOBAL OPTIONS:\n"
        "    -h, --help               Show help message\n"
        "    -V, --version            Show Carven version\n"
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

    if (std::ranges::any_of(args, [](const char* arg) static noexcept {
            const auto value = std::string_view(arg);
            return value == "--help" || value == "-h";
        })) {
        return print_help();
    }

    if (std::ranges::any_of(args, [](const char* arg) static noexcept {
            const auto value = std::string_view(arg);
            return value == "--version" || value == "-V";
        })) {
        return print_version();
    }

    return run_compile_command(args);
}
