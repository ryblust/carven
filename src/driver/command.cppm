export module zero.driver.command;

import zero.driver.pipeline;
import std;

export struct Flag final {
    std::string_view long_name;
    std::string_view short_name;
    std::string_view description;
};

export struct Command final {
    std::string_view name;
    std::string_view description;
    std::span<const Flag> flags;
    auto (*handler)(const Driver& driver) -> int;
};

export auto render_help() noexcept -> int;
export auto render_version() noexcept -> int;
export auto render_command_help(Command command) noexcept -> int;
export auto find_command(std::string_view name) noexcept -> std::optional<Command>;
