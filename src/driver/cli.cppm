export module zero.driver.cli;

import zero.driver.command;
import zero.driver.pipeline;
import std;

auto dispatch(Command cmd, int argc, char** argv) noexcept -> int {
    for (auto i = 2; i < argc; ++i) {
        const auto f = std::string_view(argv[i]);
        if (f == "--help" || f == "-h") return render_command_help(cmd);
    }

    auto driver = Driver();
    if (!driver.parse_cli_args(argc, argv)) return 1;
    if (!driver.has_input_files()) {
        std::println("zero {}: error: no input file", cmd.name);
        return 1;
    }

    return cmd.handler(driver);
}

export auto __zero_main__(int argc, char** argv) noexcept -> int {
    if (argc == 1) return render_help();
    const auto arg = std::string_view(argv[1]);
    if (arg ==    "--help" || arg == "-h")  return render_help();
    if (arg == "--version" || arg == "-V")  return render_version();
    if (const auto cmd = find_command(arg)) return dispatch(*cmd, argc, argv);

    std::println("zero: error: unknown command '{}'", arg);
    std::println("Run 'zero --help' for usage information.");
    return 1;
}
