module carven:analyzer.project.impl;

import :analyzer.project;
import :analyzer.protocol;
import :driver.sources;
import :support.path;
import std;

auto analyzer_file_uri(std::string_view path) noexcept -> std::string {
#if defined(_WIN32)
    auto normalized = std::string(path);
    std::ranges::replace(normalized, '\\', '/');
    if (normalized.size() >= 3 && normalized[1] == ':' && normalized[2] == '/') {
        if (normalized[0] >= 'a' && normalized[0] <= 'z') {
            normalized[0] -= 'a' - 'A';
        }
        normalized.insert(0, "/");
    }
#else
    const auto normalized = path;
#endif
    auto result = std::string("file://");
#if defined(_WIN32)
    if (normalized.starts_with("//")) {
        result = "file:";
    }
#endif
    constexpr auto hex = std::string_view("0123456789ABCDEF");
    // Match Node's file URLs, which encode '~' in Windows short-name paths too.
    for (const auto ch : normalized) {
        const auto byte = static_cast<unsigned char>(ch);
        if ((byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9')
            || ch == '/'
            || ch == '-'
            || ch == '_'
            || ch == '.'
            || ch == ':') {
            result += ch;
        } else {
            result += '%';
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
    }
    return result;
}

auto analyzer_file_path(std::string_view uri) noexcept -> std::optional<std::string> {
    if (!uri.starts_with("file://")) {
        return std::nullopt;
    }
    uri.remove_prefix(7);
    if (uri.starts_with("localhost/")) {
        uri.remove_prefix(9);
    }
    const auto network = uri.empty() || uri.front() != '/';
    if (network) {
#if defined(_WIN32)
        if (uri.empty() || uri.find('/') == std::string_view::npos) {
            return std::nullopt;
        }
#else
        return std::nullopt;
#endif
    }
    auto result = std::string(network ? "//" : "");
    const auto digit = [](char ch) static noexcept -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    for (auto i = 0uz; i < uri.size(); ++i) {
        if (uri[i] == '%') {
            if (i + 2 >= uri.size() || digit(uri[i + 1]) < 0 || digit(uri[i + 2]) < 0) {
                return std::nullopt;
            }
            const auto byte = static_cast<char>(digit(uri[i + 1]) * 16 + digit(uri[i + 2]));
            if (byte == '\0') {
                return std::nullopt;
            }
            result += byte;
            i += 2;
        } else {
            result += uri[i];
        }
    }
#if defined(_WIN32)
    if (result.size() >= 4 && result[0] == '/' && result[2] == ':' && result[3] == '/') {
        result.erase(0, 1);
    }
#endif
    return result;
}

auto read_analyzer_project_source(std::string_view path) noexcept -> std::optional<std::string> {
    auto file = std::ifstream(path_from_utf8(path), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

auto discover_analyzer_projects(std::span<const std::string> roots) noexcept
    -> std::vector<std::string> {
    auto found = std::set<std::string>();
    for (const auto& uri : roots) {
        const auto decoded = analyzer_file_path(uri);
        if (!decoded) {
            continue;
        }
        auto directory = path_from_utf8(*decoded).lexically_normal();
        for (;;) {
            auto code = std::error_code();
            if (std::filesystem::is_regular_file(directory / "build.cv", code)
                || std::filesystem::is_regular_file(directory / "xmake.lua", code)) {
                found.insert(path_to_generic_utf8(directory));
                break;
            }
            const auto parent = directory.parent_path();
            if (parent == directory || parent.empty()) {
                break;
            }
            directory = parent;
        }
    }
    return {found.begin(), found.end()};
}

auto load_analyzer_project(std::span<const AnalyzerProjectInput> models) noexcept
    -> AnalyzerProject {
    auto result = AnalyzerProject {.sources = {}, .targets = {}, .warnings = {}};
    const auto load_sources = [&](const CommandSources& collected) noexcept {
        auto documents = std::vector<std::string>();
        for (const auto& input : collected.carven) {
            const auto uri = analyzer_file_uri(input.path);
            const auto standard =
                std::ranges::contains(collected.standard_modules, input.module_path);
            const auto found = result.sources.find(uri);
            if (found == result.sources.end()) {
                const auto content = read_analyzer_project_source(input.path);
                if (!content) {
                    result.warnings.push_back("Cannot read project source: " + input.path);
                    continue;
                }
                result.sources.emplace(
                    uri,
                    AnalyzerProjectSource {
                        .path = input.path,
                        .module_path = input.module_path,
                        .text = *content,
                        .standard = standard
                    }
                );
            } else {
                found->second.module_path = input.module_path;
                found->second.standard = standard;
            }
            documents.push_back(uri);
        }
        return documents;
    };
    const auto crafts = locate_standard_crafts();
    auto standard = collect_toolchain_sources(crafts, {}, {});
    if (!standard) {
        result.warnings.push_back(standard.error());
        return result;
    }
    std::erase_if(standard->carven, [&](const auto& input) noexcept {
        return !std::ranges::contains(standard->standard_modules, input.module_path);
    });
    static_cast<void>(load_sources(*standard));

    for (const auto& model : models) {
        if (!model.available) {
            continue;
        }
        const auto& configured_root = model.root;
        for (const auto& target : model.targets) {
            auto inputs = std::vector<std::string_view>();
            for (const auto& input : target.sources) {
                if (path_from_utf8(input).extension() == ".cv") {
                    inputs.push_back(input);
                }
            }
            const auto collected =
                collect_toolchain_sources(crafts, inputs, {}, path_from_utf8(configured_root));
            if (!collected) {
                result.warnings.push_back(collected.error());
                continue;
            }
            auto output = AnalyzerProjectTarget {
                .name = target.name,
                .default_target = target.default_target,
                .project_root = collected->project_root,
                .roots = collected->owned_modules,
                .documents = load_sources(*collected)
            };
            if (!output.documents.empty()) {
                result.targets.push_back(std::move(output));
            }
        }
    }
    return result;
}
