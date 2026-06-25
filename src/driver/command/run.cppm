export module carven.driver.command.run;

import carven.common.process;
import carven.common.filesystem;
import carven.backend.codegen;
import carven.driver.xmake;
import std;

export struct RunCommand final {
    static constexpr auto name = "run";
    static constexpr auto description = "Run a .cv file or xmake target";
    static constexpr auto help_message =
        R"(carven run - Run a .cv file or xmake target

USAGE:
    carven run [options...] [<source-file>|<target>] [--] [args...]

OPTIONS:
    -std=c++<value>    Target C++ standard for single-file runs
    --import-std       Force #include std headers (auto if source has import std)
)";

    CodegenOptions options;
    std::optional<std::string_view> input;
    std::vector<std::string_view> args;
};

export auto execute(const RunCommand& command) noexcept -> int;

module :private;

namespace {

auto run(const RunCommand& command) noexcept -> int {
    auto xmake_dir = std::string();
    auto xmake_target = std::string();

    if (command.input && std::filesystem::path(*command.input).extension() == ".cv") {
        const auto source_file = *command.input;
        const auto source_path = std::filesystem::path(source_file);
        auto error = std::error_code();

        if (!std::filesystem::is_regular_file(source_path, error) || error) {
            std::println("carven run: error: cannot read '{}'", source_file);
            return 1;
        }

        const auto absolute_source = std::filesystem::absolute(source_path, error);

        if (error) {
            std::println("carven run: error: cannot resolve '{}'", source_file);
            return 1;
        }

        const auto absolute_source_path = absolute_source.generic_string();
        const auto target_name = sanitize_xmake_target_name(absolute_source.stem().generic_string());

        auto hash = 14695981039346656037ull;
        for (const auto ch : absolute_source_path) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ull;
        }

        const auto id = std::format("{}-{:016x}", target_name, hash);
        const auto current = std::filesystem::current_path(error);
        const auto cache_dir = error ? std::filesystem::path(".carven") : current / ".carven";
        const auto root = cache_dir / "scripts" / id;

        std::filesystem::create_directories(root, error);

        if (error) {
            std::println("carven run: error: cannot write temporary xmake project '{}'", root.generic_string());
            return 1;
        }

        if (!write_carven_xmake_rule(root)) {
            std::println("carven run: error: cannot write temporary xmake project '{}'", root.generic_string());
            return 1;
        }

        const auto source_files = std::array { absolute_source_path };
        if (!write_file(root / "xmake.lua", generate_xmake_project_file(target_name, source_files, command.options))) {
            std::println("carven run: error: cannot write temporary xmake project '{}'", root.generic_string());
            return 1;
        }

        xmake_dir = root.generic_string();
        xmake_target = target_name;
    } else {
        if (!command.input && !command.args.empty()) {
            std::println("carven run: error: project runtime args require an explicit target");
            return 1;
        }

        const auto local_project_dir = local_xmake_project_dir();

        if (!local_project_dir) {
            std::println("carven run: error: cannot find xmake.lua in current directory");
            return 1;
        }

        xmake_dir = *local_project_dir;
        xmake_target = std::string(command.input.value_or(std::string_view()));
    }

    auto args = std::vector<std::string>();
    args.reserve(4 + (xmake_target.empty() ? 0uz : 1uz) + command.args.size());
    args.emplace_back("xmake");
    args.emplace_back("run");
    args.emplace_back("-F");
    args.emplace_back("xmake.lua");

    if (!xmake_target.empty()) {
        args.emplace_back(xmake_target);
    }

    for (const auto arg : command.args) {
        args.emplace_back(arg);
    }

    const auto exit_code = spawn(args, xmake_dir);

    if (exit_code < 0) {
        std::println("carven run: error: cannot start xmake");
        return 1;
    }

    return exit_code;
}

}

auto execute(const RunCommand& command) noexcept -> int {
    return run(command);
}
