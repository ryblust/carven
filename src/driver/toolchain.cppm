export module carven.driver.toolchain;

import std;

export struct BuildArtifacts final {
    std::filesystem::path cpp_path;
    std::filesystem::path exe_path;
};

auto source_stem(std::string_view filename) noexcept -> std::string {
    const auto path = std::filesystem::path(filename);
    const auto stem = path.stem().string();

    return stem.empty() ? "main" : stem;
}

auto executable_extension() noexcept -> std::string_view {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

export auto build_artifacts(std::string_view filename, const std::filesystem::path& output_dir) noexcept -> BuildArtifacts {
    const auto base_name = source_stem(filename);

    return {
        .cpp_path = output_dir / std::format("{}.cpp", base_name),
        .exe_path = output_dir / std::format("{}{}", base_name, executable_extension())
    };
}

export auto needs_compile(const std::filesystem::path& cpp_path, const std::filesystem::path& exe_path) noexcept -> bool {
    auto error = std::error_code();
    if (!std::filesystem::exists(exe_path, error)) return true;

    const auto cpp_time = std::filesystem::last_write_time(cpp_path, error);
    if (error) return true;

    const auto exe_time = std::filesystem::last_write_time(exe_path, error);
    if (error) return true;

    return cpp_time > exe_time;
}

export auto compiler_from_environment(std::string_view fallback) noexcept -> std::string {
    const auto compiler = std::getenv("CXX");
    return (compiler != nullptr && std::string_view(compiler).size() > 0) ? compiler : fallback.data();
}
