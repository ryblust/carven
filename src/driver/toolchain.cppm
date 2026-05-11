export module zero.driver.toolchain;

import std;

export struct BuildArtifacts final {
    std::filesystem::path cpp_path;
    std::filesystem::path exe_path;
};

export auto build_artifacts(std::string_view filename, const std::filesystem::path& output_dir) noexcept -> BuildArtifacts;

export auto needs_compile(const std::filesystem::path& cpp_path, const std::filesystem::path& exe_path) noexcept -> bool;

export auto compiler_from_environment(std::string_view fallback) noexcept -> std::string;

export auto display_command(std::span<const std::string> args) noexcept -> std::string;
