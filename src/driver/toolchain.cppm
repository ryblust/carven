export module zero.driver.toolchain;

import std;

export struct BuildArtifacts final {
    std::filesystem::path cpp_path;
    std::filesystem::path exe_path;
};

auto source_stem(std::string_view filename) noexcept -> std::string {
    auto path = std::filesystem::path(filename);
    auto stem = path.stem().string();

    return stem.empty() ? "main" : stem;
}

constexpr auto fnv1a_64(std::string_view value) noexcept -> std::uint64_t {
    auto hash = 14695981039346656037ull;

    for (const auto c : value) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }

    return hash;
}

auto short_hash(std::string_view value) noexcept -> std::string {
    return std::format("{:08x}", static_cast<std::uint32_t>(fnv1a_64(value)));
}

auto stable_source_path(std::string_view filename) noexcept -> std::filesystem::path {
    auto error = std::error_code();
    auto path = std::filesystem::weakly_canonical(filename, error);

    if (!error) return path;
    path = std::filesystem::absolute(filename, error);
    if (!error) return path;

    return filename;
}

constexpr auto executable_extension() noexcept -> std::string_view {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

export auto build_artifacts(std::string_view filename, const std::filesystem::path& output_dir) noexcept -> BuildArtifacts {
    const auto source_path = stable_source_path(filename);
    const auto base_name = std::format("{}_{}", source_stem(filename), short_hash(source_path.string()));

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
    if (const auto* value = std::getenv("CXX"); value != nullptr && std::string_view(value).size() > 0) {
        return value;
    }

    return std::string(fallback);
}

export constexpr auto display_command(std::span<const std::string> args) noexcept -> std::string {
    auto result = std::string();

    for (auto i = 0uz; i < args.size(); ++i) {
        if (i > 0) result.push_back(' ');
        const auto needs_quotes = args[i].contains(' ') || args[i].contains('\t');

        if (needs_quotes) result.push_back('"');
        result.append(args[i]);

        if (needs_quotes) result.push_back('"');
    }

    return result;
}
