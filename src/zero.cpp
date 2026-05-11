import zero.driver.command;
import zero.driver.pipeline;
import std;

auto main(int argc, const char** argv) noexcept -> int {
    if (argc == 1) return render_help();

    const auto arg = std::string_view(argv[1]);
    if (arg ==    "--help" || arg == "-h")  return render_help();
    if (arg == "--version" || arg == "-V")  return render_version();

    const auto command = find_command(arg);
    if (!command) {
        std::println("zero: error: unknown command '{}'", arg);
        std::println("Run 'zero --help' for usage information.");
        return 1;
    }

    if (argc > 2) {
        const auto flag = std::string_view(argv[2]);
        if (flag == "--help" || flag == "-h") return render_command_help(*command);
    }

    auto driver = Driver();
    if (!driver.parse_flags({argv + 2, static_cast<std::size_t>(argc - 2)})) return 1;
    if (!driver.has_input_files()) {
        std::println("zero {}: error: no input file", command->name);
        return 1;
    }

    return command->handler(driver);
}
