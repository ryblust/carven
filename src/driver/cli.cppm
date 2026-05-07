export module zero.driver.cli;

import zero.driver.command;
import zero.driver.pipeline;
import std;

export auto __zero_main__(int argc, char** argv) noexcept -> int {
    const auto args = std::span(argv, static_cast<std::size_t>(argc))
        | std::views::transform([](const char* s) noexcept { return std::string_view(s); })
        | std::ranges::to<std::vector<std::string_view>>();

    if (args.size() == 1) return render_help();

    if (args[1] ==    "--help" || args[1] == "-h")  return render_help();
    if (args[1] == "--version" || args[1] == "-V")  return render_version();

    const auto command = find_command(args[1]);
    if (!command) {
        std::println("zero: error: unknown command '{}'", args[1]);
        std::println("Run 'zero --help' for usage information.");
        return 1;
    }

    if (args.size() > 2) {
        if (args[2] == "--help" || args[2] == "-h") return render_command_help(*command);
    }

    auto driver = Driver();
    if (!driver.parse_flags(args | std::views::drop(2))) return 1;
    if (!driver.has_input_files()) {
        std::println("zero {}: error: no input file", command->name);
        return 1;
    }

    return command->handler(driver);
}
