export module carven.driver.xmake;

import carven.common.filesystem;
import carven.backend.codegen;
import std;

export auto sanitize_xmake_target_name(std::string_view name) noexcept -> std::string;
export auto generate_xmake_project_file(std::string_view project_name, std::span<const std::string> source_files, CodegenOptions options) noexcept -> std::string;
export auto write_carven_xmake_rule(const std::filesystem::path& project_dir) noexcept -> bool;
export auto local_xmake_project_dir() noexcept -> std::optional<std::string>;

module :private;

namespace {

constexpr auto lua_literal(std::string_view value) noexcept -> std::string {
    auto result = std::string("\"");
    result.reserve(value.size() + 2);

    for (const auto ch : value) {
        if (ch == '\\' || ch == '"') {
            result += '\\';
        }
        result += ch;
    }

    return result += '"';
}

constexpr auto embedded_carven_rule() noexcept -> std::string_view {
    static constexpr const char rule[] = {
        #embed "xmake/rules/carven.lua"
    };

    return std::string_view(rule, sizeof(rule));
}

} // namespace

auto sanitize_xmake_target_name(std::string_view name) noexcept -> std::string {
    static constexpr auto is_identifier_char = [](char ch) static noexcept {
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

auto generate_xmake_project_file(std::string_view project_name, std::span<const std::string> source_files, CodegenOptions options) noexcept -> std::string {
    const auto standard_text = std::format("c++{}", options.language_standard);

    auto content = std::format(
        "set_project({})\n"
        "add_rules(\"mode.debug\", \"mode.release\")\n"
        "set_languages(\"{}\")\n"
        "set_defaultmode(\"debug\")\n"
        "\n"
        "includes(\"xmake/rules/carven.lua\")\n"
        "\n"
        "target({})\n"
        "    set_kind(\"binary\")\n"
        "    add_rules(\"carven\")\n"
        "    set_values(\"carven.standard\", {})\n",
        lua_literal(project_name),
        standard_text,
        lua_literal(project_name),
        lua_literal(standard_text)
    );

    if (options.import_std) {
        content += "    set_values(\"carven.import_std\", true)\n";
    }

    for (const auto& file : source_files) {
        content += std::format("    add_files({})\n", lua_literal(file));
    }

    return content;
}

auto write_carven_xmake_rule(const std::filesystem::path& project_dir) noexcept -> bool {
    const auto rule_dir = project_dir / "xmake" / "rules";
    auto error = std::error_code();

    std::filesystem::create_directories(rule_dir, error);
    if (error) {
        return false;
    }

    return write_file(rule_dir / "carven.lua", embedded_carven_rule());
}

auto local_xmake_project_dir() noexcept -> std::optional<std::string> {
    auto error = std::error_code();
    const auto current = std::filesystem::current_path(error);

    if (error || !std::filesystem::is_regular_file("xmake.lua", error) || error) {
        return std::nullopt;
    }

    return current.generic_string();
}
