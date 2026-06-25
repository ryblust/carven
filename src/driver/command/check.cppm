export module carven.driver.command.check;

import std;

export struct CheckCommand final {
    static constexpr auto name = "check";
    static constexpr auto description = "Parse and check without codegen";
    static constexpr auto help_message =
        R"(carven check - Parse and check without codegen

USAGE:
    carven check <source-file>

No command-specific options.
)";

    std::string_view source_file;
};

export auto execute(const CheckCommand& command) noexcept -> int;

module :private;

auto execute([[maybe_unused]] const CheckCommand& command) noexcept -> int {
    std::println("carven check: not yet implemented");
    return 1;
}
