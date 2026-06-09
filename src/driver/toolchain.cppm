export module carven.driver.toolchain;

import std;

export struct SingleFileProject final {
    std::string root_dir;
    std::string target_name;
};

constexpr auto source_stem(std::string_view filename) noexcept -> std::string {
    const auto last_sep = filename.find_last_of("/\\");
    const auto name_start = last_sep == std::string_view::npos ? 0uz : last_sep + 1;
    const auto name = filename.substr(name_start);
    const auto last_dot = name.find_last_of('.');
    if (last_dot == std::string_view::npos || last_dot == 0) return std::string(name);
    return std::string(name.substr(0, last_dot));
}

constexpr auto stable_hash(std::string_view text) noexcept -> std::uint64_t {
    auto hash = 14695981039346656037ull;
    for (const auto ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

constexpr auto is_identifier_char(char ch) noexcept -> bool {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

export constexpr auto is_carven_source_path(std::string_view path) noexcept -> bool {
    return path.ends_with(".cv");
}

export auto sanitize_target_name(std::string_view name) noexcept -> std::string {
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

auto lua_string_literal(std::string_view value) noexcept -> std::string {
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

auto cxx_standard_text(std::uint8_t standard) noexcept -> std::string {
    return std::format("c++{}", standard);
}

auto environment_path(const char* name) noexcept -> std::optional<std::filesystem::path> {
    const auto value = std::getenv(name);
    if (value == nullptr || std::string_view(value).empty()) return std::nullopt;
    return std::filesystem::path(value);
}

auto is_existing_file(const std::filesystem::path& path) noexcept -> bool {
    auto error = std::error_code();
    return std::filesystem::is_regular_file(path, error) && !error;
}

auto absolute_path(std::filesystem::path path) noexcept -> std::optional<std::filesystem::path> {
    auto error = std::error_code();
    path = std::filesystem::absolute(path, error);
    if (error) return std::nullopt;
    return path;
}

auto has_path_separator(std::string_view path) noexcept -> bool {
    return path.find('/') != std::string_view::npos || path.find('\\') != std::string_view::npos;
}

auto path_separator() noexcept -> char {
#if defined(_WIN32)
    return ';';
#else
    return ':';
#endif
}

auto executable_candidates(const std::filesystem::path& directory, std::string_view program) noexcept -> std::vector<std::filesystem::path> {
    auto candidates = std::vector<std::filesystem::path>();
    candidates.emplace_back(directory / std::string(program));
#if defined(_WIN32)
    if (!program.ends_with(".exe")) {
        candidates.emplace_back(directory / std::format("{}.exe", program));
    }
#endif
    return candidates;
}

auto find_program_on_path(std::string_view program) noexcept -> std::optional<std::filesystem::path> {
    const auto path_value = std::getenv("PATH");
    if (path_value == nullptr) return std::nullopt;

    const auto path_text = std::string_view(path_value);
    auto offset = 0uz;

    while (offset <= path_text.size()) {
        const auto next = path_text.find(path_separator(), offset);
        const auto entry = path_text.substr(offset, next == std::string_view::npos ? std::string_view::npos : next - offset);
        if (!entry.empty()) {
            for (const auto& candidate : executable_candidates(std::filesystem::path(entry), program)) {
                if (is_existing_file(candidate)) return candidate;
            }
        }

        if (next == std::string_view::npos) break;
        offset = next + 1;
    }

    return std::nullopt;
}

auto find_rule_in_ancestors(std::filesystem::path start) noexcept -> std::optional<std::filesystem::path> {
    auto error = std::error_code();
    if (!std::filesystem::is_directory(start, error)) {
        start = start.parent_path();
    }

    auto current = std::filesystem::absolute(start, error);
    if (error) return std::nullopt;

    while (!current.empty()) {
        const auto candidate = current / "xmake" / "rules" / "carven.lua";
        if (is_existing_file(candidate)) return candidate;

        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    return std::nullopt;
}

auto installed_rule_from_program(std::string_view program) noexcept -> std::optional<std::filesystem::path> {
    const auto program_path = has_path_separator(program)
        ? std::optional { std::filesystem::path(program) }
        : find_program_on_path(program);
    if (!program_path) return std::nullopt;

    const auto absolute = absolute_path(*program_path);
    if (!absolute) return std::nullopt;

    const auto prefix = absolute->parent_path().parent_path();
    const auto candidate = prefix / "share" / "carven" / "xmake" / "rules" / "carven.lua";
    if (is_existing_file(candidate)) return candidate;

    return find_rule_in_ancestors(absolute->parent_path());
}

auto user_cache_dir() noexcept -> std::filesystem::path {
#if defined(_WIN32)
    if (const auto local = environment_path("LOCALAPPDATA")) {
        return *local / "carven" / "cache";
    }
    if (const auto temp = environment_path("TEMP")) {
        return *temp / "carven";
    }
    return std::filesystem::temp_directory_path() / "carven";
#elif defined(__APPLE__)
    if (const auto home = environment_path("HOME")) {
        return *home / "Library" / "Caches" / "carven";
    }
    return std::filesystem::temp_directory_path() / "carven";
#else
    if (const auto xdg = environment_path("XDG_CACHE_HOME")) {
        return *xdg / "carven";
    }
    if (const auto home = environment_path("HOME")) {
        return *home / ".cache" / "carven";
    }
    return std::filesystem::temp_directory_path() / "carven";
#endif
}

export auto single_file_project(std::string_view filename) noexcept -> SingleFileProject {
    const auto target_name = sanitize_target_name(source_stem(filename));
    const auto hash = stable_hash(filename);
    const auto id = std::format("{}-{:016x}", target_name, hash);
    const auto root = user_cache_dir() / "scripts" / id;

    return {
        .root_dir = root.generic_string(),
        .target_name = target_name,
    };
}

export auto find_carven_rule(std::string_view carven_program) noexcept -> std::optional<std::string> {
    if (const auto override_rule = environment_path("CARVEN_RULE")) {
        if (is_existing_file(*override_rule)) return override_rule->generic_string();
    }

    if (const auto installed_rule = installed_rule_from_program(carven_program)) {
        return installed_rule->generic_string();
    }

    auto error = std::error_code();
    const auto cwd = std::filesystem::current_path(error);
    if (!error) {
        if (const auto source_rule = find_rule_in_ancestors(cwd)) {
            return source_rule->generic_string();
        }
    }

    return std::nullopt;
}

export auto find_project_root(std::filesystem::path start) noexcept -> std::optional<std::string> {
    auto error = std::error_code();
    if (start.empty()) start = std::filesystem::current_path(error);
    if (error) return std::nullopt;

    if (!std::filesystem::is_directory(start, error)) {
        start = start.parent_path();
    }

    auto current = std::filesystem::absolute(start, error);
    if (error) return std::nullopt;

    while (!current.empty()) {
        if (std::filesystem::exists(current / "xmake.lua", error) && !error) {
            return current.generic_string();
        }

        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    return std::nullopt;
}

export auto build_xmake_build_args(std::string_view target = "") noexcept -> std::vector<std::string> {
    auto args = std::vector<std::string>();
    args.reserve(target.empty() ? 4 : 5);
    args.emplace_back("xmake");
    args.emplace_back("build");
    args.emplace_back("-F");
    args.emplace_back("xmake.lua");
    if (!target.empty()) args.emplace_back(target);
    return args;
}

export auto build_xmake_run_args(std::string_view target,
                                 std::span<const std::string_view> forwarded_args) noexcept -> std::vector<std::string> {
    auto args = std::vector<std::string>();
    args.reserve(4 + (target.empty() ? 0uz : 1uz) + forwarded_args.size());
    args.emplace_back("xmake");
    args.emplace_back("run");
    args.emplace_back("-F");
    args.emplace_back("xmake.lua");
    if (!target.empty()) args.emplace_back(target);
    for (const auto arg : forwarded_args) {
        args.emplace_back(arg);
    }
    return args;
}

export auto generated_project_xmake(std::string_view project_name,
                                    std::span<const std::string> source_files,
                                    std::uint8_t standard) noexcept -> std::string {
    const auto standard_text = cxx_standard_text(standard);
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
        lua_string_literal(standard_text)
    );

    for (const auto& file : source_files) {
        content += std::format("    add_files({})\n", lua_string_literal(file));
    }

    return content;
}

export auto generated_single_file_xmake(std::string_view target_name,
                                        std::string_view absolute_source_path,
                                        std::uint8_t standard,
                                        std::string_view carven_program = {},
                                        bool import_std = false) noexcept -> std::string {
    const auto standard_text = cxx_standard_text(standard);
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
        target_name,
        standard_text,
        target_name,
        lua_string_literal(standard_text)
    );

    if (!carven_program.empty()) {
        content += std::format("    set_values(\"carven.program\", {})\n", lua_string_literal(carven_program));
    }
    if (import_std) {
        content += "    set_values(\"carven.import_std\", true)\n";
    }

    content += std::format("    add_files({})\n", lua_string_literal(absolute_source_path));
    return content;
}

export auto write_carven_rule(const std::filesystem::path& project_dir, std::string_view carven_program) noexcept -> bool {
    const auto source_rule = find_carven_rule(carven_program);
    if (!source_rule) return false;

    const auto rule_dir = project_dir / "xmake" / "rules";
    auto error = std::error_code();
    std::filesystem::create_directories(rule_dir, error);
    if (error) return false;

    std::filesystem::copy_file(
        *source_rule,
        rule_dir / "carven.lua",
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    return !error;
}

export auto write_single_file_xmake(const SingleFileProject& project,
                                    std::string_view absolute_source_path,
                                    std::uint8_t standard,
                                    std::string_view carven_program,
                                    bool import_std) noexcept -> bool {
    const auto root = std::filesystem::path(project.root_dir);

    auto error = std::error_code();
    std::filesystem::create_directories(root, error);
    if (error) return false;
    if (!write_carven_rule(root, carven_program)) return false;

    auto file = std::ofstream(root / "xmake.lua", std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    const auto content = generated_single_file_xmake(
        project.target_name,
        absolute_source_path,
        standard,
        carven_program,
        import_std
    );
    file << content;
    return file.good();
}
