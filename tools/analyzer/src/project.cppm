module carven:analyzer.project;

import :analyzer.protocol;
import :source.module_path;
import std;

struct AnalyzerProjectSource final {
    std::string path;
    CanonicalModulePath module_path;
    std::string text;
    bool standard;
};

struct AnalyzerProjectTarget final {
    std::string name;
    bool default_target;
    std::filesystem::path project_root;
    std::vector<CanonicalModulePath> roots;
    std::vector<std::string> documents;
};

struct AnalyzerProject final {
    std::map<std::string, AnalyzerProjectSource, std::less<>> sources;
    std::vector<AnalyzerProjectTarget> targets;
    std::vector<std::string> warnings;
};

auto analyzer_file_uri(std::string_view path) noexcept -> std::string;
auto analyzer_file_path(std::string_view uri) noexcept -> std::optional<std::string>;
auto read_analyzer_project_source(std::string_view path) noexcept -> std::optional<std::string>;

auto discover_analyzer_projects(std::span<const std::string> roots) noexcept
    -> std::vector<std::string>;

auto load_analyzer_project(std::span<const AnalyzerProjectInput> models) noexcept
    -> AnalyzerProject;
