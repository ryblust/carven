export module carven.driver.toolchain;

import std;

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

auto workspace_cache_dir() noexcept -> std::filesystem::path {
    auto error = std::error_code();
    const auto current = std::filesystem::current_path(error);
    if (error) return std::filesystem::path(".carven");
    return current / ".carven";
}

export struct SingleFileConfig final {
    std::string absolute_source_path;
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
    const auto root = workspace_cache_dir() / "scripts" / id;

    config.root_dir = root.generic_string();
    config.target_name = target_name;
    return config;
}

export constexpr auto embedded_carven_rule() noexcept -> std::string_view {
    static constexpr const char rule[] = {
        #embed "xmake/rules/carven.lua"
    };

    return rule;
}

export auto find_project_root(std::filesystem::path start) noexcept -> std::optional<std::string> {
    auto error = std::error_code();
    if (start.empty()) start = std::filesystem::current_path(error);
    if (error) return std::nullopt;

    if (!std::filesystem::is_directory(start, error)) {
        if (error) return std::nullopt;
        start = start.parent_path();
    }

    error.clear();
    const auto root = std::filesystem::absolute(start, error);
    if (error) return std::nullopt;

    error.clear();
    if (!std::filesystem::is_regular_file(root / "xmake.lua", error) || error) return std::nullopt;
    return root.generic_string();
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

    if (config.import_std) {
        content += "    set_values(\"carven.import_std\", true)\n";
    }

    content += std::format("    add_files({})\n", lua_literal(config.absolute_source_path));
    return content;
}

export auto write_xmake_carven_rule(const std::filesystem::path& project_dir) noexcept -> bool {
    const auto rule_dir = project_dir / "xmake" / "rules";
    auto error = std::error_code();
    std::filesystem::create_directories(rule_dir, error);
    if (error) return false;

    auto file = std::ofstream(rule_dir / "carven.lua", std::ios::binary | std::ios::trunc);
    const auto rule = embedded_carven_rule();
    return file.is_open() && file.write(rule.data(), rule.size()).good();
}

export auto write_xmake_single_file(const SingleFileConfig& config) noexcept -> bool {
    const auto root = std::filesystem::path(config.root_dir);

    auto error = std::error_code();
    std::filesystem::create_directories(root, error);

    if (error) return false;
    if (!write_xmake_carven_rule(root)) return false;

    auto file = std::ofstream(root / "xmake.lua", std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file << generate_xmake_single_file(config);
    return file.good();
}
