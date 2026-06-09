import carven.driver.command;
import std;

auto main(int argc, const char** argv) noexcept -> int {
    if (argc == 1) return render_help();

    const auto arg = std::string_view(argv[1]);
    if (arg ==    "--help" || arg == "-h")  return render_help();
    if (arg == "--version" || arg == "-V")  return render_version();

    const auto command = find_command(arg);
    if (!command) {
        std::println("carven: error: unknown command '{}'", arg);
        std::println("Run 'carven --help' for usage information.");
        return 1;
    }

    if (argc > 2) {
        const auto flag = std::string_view(argv[2]);
        if (flag == "--help" || flag == "-h") return render_command_help(*command);
    }

    const auto invocation = parse_command(arg, {argv + 2, static_cast<std::size_t>(argc - 2)}, argv[0]);
    if (!invocation) return render_command_error(invocation.error());

    return dispatch(*invocation);
}
