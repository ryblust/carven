module carven:analyzer.documents;

import :source.module_path;
import std;

struct AnalyzerBuffer final {
    std::int64_t version;
    std::string text;
};

struct AnalyzerOpenDocument final {
    CanonicalModulePath module_path;
    std::map<std::string, AnalyzerBuffer, std::less<>> buffers;
};

class AnalyzerDocuments final {
    std::map<std::string, AnalyzerOpenDocument, std::less<>> documents;
    std::map<std::string, std::string, std::less<>> bindings;
    std::uint64_t next_module_id = 0;

    static auto document_identity(std::string_view uri) noexcept -> std::string;

public:
    auto resolve(std::string_view uri) const noexcept -> std::string;

    auto module_path(std::string_view document, std::span<const std::string> roots) noexcept
        -> CanonicalModulePath;

    auto open(
        std::string_view uri,
        std::string document,
        CanonicalModulePath module_path,
        std::int64_t version,
        std::string text
    ) noexcept -> bool;

    auto change(std::string_view uri, std::int64_t version, std::string text) noexcept -> bool;
    auto close(std::string_view uri) noexcept -> std::optional<std::string>;

    auto entries() const noexcept
        -> const std::map<std::string, AnalyzerOpenDocument, std::less<>>&;

    auto contains(std::string_view document) const noexcept -> bool;
    auto conflicted(std::string_view document) const noexcept -> bool;

    // Borrowed until the next registry mutation.
    auto view(std::string_view document, std::string_view context) const noexcept
        -> const AnalyzerBuffer*;

    auto client_uris(std::string_view context) const noexcept
        -> std::map<std::string, std::string, std::less<>>;
};
