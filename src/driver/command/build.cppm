export module carven.driver.command.build;

import carven.common.process;
import carven.driver.xmake;
import std;

export struct BuildCommand final {
    static constexpr auto name = "build";
    static constexpr auto description = "Build an xmake project or target";
    static constexpr auto help_message =
        R"(carven build - Build an xmake project or target

USAGE:
    carven build [target]

No command-specific options.
)";

    std::optional<std::string_view> target;
};

export auto execute(const BuildCommand& command) noexcept -> int;

module :private;

namespace {

auto build(std::optional<std::string_view> target) noexcept -> int {
    const auto project_dir = local_xmake_project_dir();

    if (!project_dir) {
        std::println("carven build: error: cannot find xmake.lua in current directory");
        return 1;
    }

    auto args = std::vector<std::string>();
    args.reserve(target ? 5 : 4);
    args.emplace_back("xmake");
    args.emplace_back("build");
    args.emplace_back("-F");
    args.emplace_back("xmake.lua");

    if (target) {
        args.emplace_back(*target);
    }

    const auto exit_code = spawn(args, *project_dir);

    if (exit_code < 0) {
        std::println("carven build: error: cannot start xmake");
        return 1;
    }

    return exit_code;
}

}

auto execute(const BuildCommand& command) noexcept -> int {
    return build(command.target);
}
