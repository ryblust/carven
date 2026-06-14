export module carven.driver.toolchain;

import std;

inline constexpr auto carven_rule_relative_path = "xmake/rules/carven.lua";

#if defined(_WIN32)
inline constexpr auto path_list_separator = ';';
#else
inline constexpr auto path_list_separator = ':';
#endif

constexpr auto compute_hash(std::string_view text) noexcept -> std::uint64_t {
    auto hash = 14695981039346656037ull;
    for (const auto ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

constexpr auto lua_literal(std::string_view value) noexcept -> std::string {
    auto result = std::string("\"");
    result.reserve(value.size() + 2);

    for (const auto ch : value) {
        if (ch == '\\' || ch == '"') {
            result += '\\';
        }
        result += ch;
    }

    result += '"';
    return result;
}

auto resolve_path(std::filesystem::path path) noexcept -> std::optional<std::filesystem::path> {
    auto error = std::error_code();
    if (std::filesystem::is_regular_file(path, error) && !error) return path;

#if defined(_WIN32)
    if (path.extension() != ".exe") {
        path += ".exe";
        error.clear();
        if (std::filesystem::is_regular_file(path, error) && !error) return path;
    }
#endif

    return std::nullopt;
}

auto find_program(std::string_view program) noexcept -> std::optional<std::filesystem::path> {
    if (program.empty()) return std::nullopt;

    const auto program_path = std::filesystem::path(program);
    if (program_path.has_parent_path()) {
        return resolve_path(program_path);
    }

    const auto path_value = std::getenv("PATH");
    if (path_value == nullptr) return std::nullopt;

    const auto path_text = std::string_view(path_value);
    for (const auto entry : std::views::split(path_text, path_list_separator)) {
        if (entry.empty()) continue;

        const auto directory = std::filesystem::path(std::string_view(entry));
        if (const auto candidate = resolve_path(directory / program_path)) return candidate;
    }

    return std::nullopt;
}

auto find_ancestor(std::filesystem::path start, const std::filesystem::path& marker) noexcept -> std::optional<std::filesystem::path> {
    auto error = std::error_code();
    if (start.empty()) start = std::filesystem::current_path(error);
    if (error) return std::nullopt;

    if (!std::filesystem::is_directory(start, error)) {
        if (error) return std::nullopt;
        start = start.parent_path();
    }

    auto current = std::filesystem::absolute(start, error);
    if (error) return std::nullopt;

    while (!current.empty()) {
        error.clear();
        if (std::filesystem::is_regular_file(current / marker, error) && !error) return current;

        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    return std::nullopt;
}

auto find_installed_rule(std::string_view program) noexcept -> std::optional<std::filesystem::path> {
    const auto program_path = find_program(program);
    if (!program_path) return std::nullopt;

    auto error = std::error_code();
    const auto absolute = std::filesystem::absolute(*program_path, error);
    if (error) return std::nullopt;

    const auto prefix = absolute.parent_path().parent_path();
    const auto candidate = prefix / "share" / "carven" / carven_rule_relative_path;
    error.clear();
    if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;

    const auto root = find_ancestor(absolute.parent_path(), carven_rule_relative_path);
    if (!root) return std::nullopt;

    return *root / carven_rule_relative_path;
}

auto user_cache_dir() noexcept -> std::filesystem::path {
#if defined(_WIN32)
    if (const auto local = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(local) / "carven";
    }
    if (const auto temp = std::getenv("TEMP")) {
        return std::filesystem::path(temp) / "carven";
    }
    return std::filesystem::temp_directory_path() / "carven";
#elif defined(__APPLE__)
    if (const auto home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Caches" / "carven";
    }
    return std::filesystem::temp_directory_path() / "carven";
#else
    if (const auto xdg = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(xdg) / "carven";
    }
    if (const auto home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache" / "carven";
    }
    return std::filesystem::temp_directory_path() / "carven";
#endif
}

export struct SingleFileConfig final {
    std::string absolute_source_path;
    std::string carven_program;
    std::uint8_t standard;
    bool import_std;
    std::string root_dir = {};
    std::string target_name = {};
};

export auto is_carven_source_path(std::string_view path) noexcept -> bool {
    return std::filesystem::path(path).extension() == ".cv";
}

export auto sanitize_target_name(std::string_view name) noexcept -> std::string {
    constexpr auto is_identifier_char = [](char ch) noexcept {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    };

    auto result = std::string();
    result.reserve(name.size() + 4);

    for (const auto ch : name) {
        result += is_identifier_char(ch) ? ch : '_';
    }

    if (result.empty() || (result[0] >= '0' && result[0] <= '9')) {
        result.insert(0, "app_");
    }

    return result;
}

export auto make_single_file_config(SingleFileConfig config) noexcept -> SingleFileConfig {
    const auto source_path = std::filesystem::path(config.absolute_source_path);
    const auto target_name = sanitize_target_name(source_path.stem().generic_string());
    const auto hash = compute_hash(config.absolute_source_path);
    const auto id = std::format("{}-{:016x}", target_name, hash);
    const auto root = user_cache_dir() / "scripts" / id;

    config.root_dir = root.generic_string();
    config.target_name = target_name;
    return config;
}

export auto find_carven_rule(std::string_view carven_program) noexcept -> std::optional<std::string> {
    if (const auto override_rule = std::getenv("CARVEN_RULE")) {
        auto error = std::error_code();
        const auto path = std::filesystem::path(override_rule);
        if (std::filesystem::is_regular_file(path, error) && !error) return path.generic_string();
    }

    if (const auto installed_rule = find_installed_rule(carven_program)) {
        return installed_rule->generic_string();
    }

    if (const auto root = find_ancestor({}, carven_rule_relative_path)) {
        return (*root / carven_rule_relative_path).generic_string();
    }

    return std::nullopt;
}

export auto find_project_root(std::filesystem::path start) noexcept -> std::optional<std::string> {
    const auto root = find_ancestor(std::move(start), "xmake.lua");
    if (!root) return std::nullopt;
    return root->generic_string();
}

export auto xmake_build_args(std::string_view target = "") noexcept -> std::vector<std::string> {
    auto args = std::vector<std::string>();

    args.reserve(target.empty() ? 4 : 5);
    args.emplace_back("xmake");
    args.emplace_back("build");
    args.emplace_back("-F");
    args.emplace_back("xmake.lua");

    if (!target.empty()) args.emplace_back(target);
    return args;
}

export auto xmake_run_args(std::string_view target, std::span<const std::string_view> forwarded_args) noexcept -> std::vector<std::string> {
    auto args = std::vector<std::string>();

    args.reserve(4 + (target.empty() ? 0uz : 1uz) + forwarded_args.size());
    args.emplace_back("xmake");
    args.emplace_back("run");
    args.emplace_back("-F");
    args.emplace_back("xmake.lua");

    if (!target.empty()) args.emplace_back(target);
    for (const auto arg : forwarded_args) args.emplace_back(arg);
    return args;
}

export auto generate_xmake_project(std::string_view project_name, std::span<const std::string> source_files, std::uint8_t standard) noexcept -> std::string {
    const auto standard_text = std::format("c++{}", standard);

    auto content = std::format(
        "set_project(\"{}\")\n"
        "add_rules(\"mode.debug\", \"mode.release\")\n"
        "set_languages(\"{}\")\n"
        "set_defaultmode(\"debug\")\n"
        "\n"
        "includes(\"xmake/rules/carven.lua\")\n"
        "\n"
        "target(\"app\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.standard\", {})\n",
        project_name,
        standard_text,
        lua_literal(standard_text)
    );

    for (const auto& file : source_files) {
        content += std::format("    add_files({})\n", lua_literal(file));
    }

    return content;
}

export auto generate_xmake_single_file(const SingleFileConfig& config) noexcept -> std::string {
    const auto standard_text = std::format("c++{}", config.standard);
    auto content = std::format(
        "set_project(\"{}\")\n"
        "add_rules(\"mode.debug\", \"mode.release\")\n"
        "set_languages(\"{}\")\n"
        "set_defaultmode(\"debug\")\n"
        "\n"
        "includes(\"xmake/rules/carven.lua\")\n"
        "\n"
        "target(\"{}\")\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.standard\", {})\n",
        config.target_name,
        standard_text,
        config.target_name,
        lua_literal(standard_text)
    );

    if (!config.carven_program.empty()) {
        content += std::format("    set_values(\"carven.program\", {})\n", lua_literal(config.carven_program));
    }
    if (config.import_std) {
        content += "    set_values(\"carven.import_std\", true)\n";
    }

    content += std::format("    add_files({})\n", lua_literal(config.absolute_source_path));
    return content;
}

export auto write_xmake_carven_rule(const std::filesystem::path& project_dir, std::string_view carven_program) noexcept -> bool {
    const auto source_rule = find_carven_rule(carven_program);
    if (!source_rule) return false;

    const auto rule_dir = project_dir / "xmake" / "rules";
    auto error = std::error_code();
    std::filesystem::create_directories(rule_dir, error);
    if (error) return false;

    std::filesystem::copy_file(*source_rule, rule_dir / "carven.lua", std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

export auto write_xmake_single_file(const SingleFileConfig& config) noexcept -> bool {
    const auto root = std::filesystem::path(config.root_dir);

    auto error = std::error_code();
    std::filesystem::create_directories(root, error);

    if (error) return false;
    if (!write_xmake_carven_rule(root, config.carven_program)) return false;

    auto file = std::ofstream(root / "xmake.lua", std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file << generate_xmake_single_file(config);
    return file.good();
}
