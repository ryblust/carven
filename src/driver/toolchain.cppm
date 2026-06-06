export module carven.driver.toolchain;

import std;

export struct BuildArtifacts final {
    std::string cpp_path;
    std::string exe_path;
};

constexpr auto source_stem(std::string_view filename) noexcept -> std::string {
    const auto last_sep = filename.find_last_of("/\\");
    const auto name_start = last_sep == std::string_view::npos ? 0uz : last_sep + 1;
    const auto name = filename.substr(name_start);
    const auto last_dot = name.find_last_of('.');
    if (last_dot == std::string_view::npos || last_dot == 0) return std::string(name);
    return std::string(name.substr(0, last_dot));
}

constexpr auto executable_extension() noexcept -> std::string_view {
#if defined(_WIN32)
    return ".exe";
#else
    return {};
#endif
}

export constexpr auto build_artifacts(std::string_view filename, std::string_view output_dir) noexcept -> BuildArtifacts {
    const auto base_name = source_stem(filename);

    return {
        .cpp_path = std::string(output_dir) + "/" + base_name + ".cpp",
        .exe_path = std::string(output_dir) + "/" + base_name + std::string(executable_extension())
    };
}

export constexpr auto build_compile_args(std::string_view compiler, std::string_view cpp_path, std::string_view exe_path, std::uint8_t standard) noexcept -> std::vector<std::string> {
    const auto std_flag = std::string("-std=c++") + static_cast<char>('0' + standard / 10) + static_cast<char>('0' + standard % 10);
    const auto is_clang_cl = compiler.contains("clang-cl");

    auto args = std::vector<std::string>();
    args.reserve(is_clang_cl ? 6 : 5);
    args.emplace_back(compiler);

    if (is_clang_cl) {
        args.emplace_back("-Xclang");
    }

    args.emplace_back(std_flag);
    args.emplace_back(cpp_path);
    args.emplace_back("-o");
    args.emplace_back(exe_path);

    return args;
}

export auto needs_compile(std::string_view cpp_path, std::string_view exe_path) noexcept -> bool {
    auto error = std::error_code();
    if (!std::filesystem::exists(exe_path, error)) return true;

    const auto cpp_time = std::filesystem::last_write_time(cpp_path, error);
    if (error) return true;

    const auto exe_time = std::filesystem::last_write_time(exe_path, error);
    if (error) return true;

    return cpp_time > exe_time;
}
