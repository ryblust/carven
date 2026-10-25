module carven:analyzer.documents.impl;

import :analyzer.documents;
import :analyzer.project;
import :source.module_path;
import :support.path;
import std;

auto AnalyzerDocuments::document_identity(std::string_view uri) noexcept -> std::string {
    auto path = analyzer_file_path(uri);
    if (!path) {
        if (!path_from_utf8(uri).is_absolute()) {
            return std::string(uri);
        }
        path = uri;
    }
    auto error = std::error_code();
    const auto physical = std::filesystem::weakly_canonical(path_from_utf8(*path), error);
    return analyzer_file_uri(error ? *path : path_to_generic_utf8(physical));
}

auto AnalyzerDocuments::resolve(std::string_view uri) const noexcept -> std::string {
    if (const auto found = bindings.find(uri); found != bindings.end()) {
        return found->second;
    }
    return document_identity(uri);
}

auto AnalyzerDocuments::module_path(
    std::string_view document,
    std::span<const std::string> roots
) noexcept -> CanonicalModulePath {
    if (const auto found = documents.find(document); found != documents.end()) {
        return found->second.module_path;
    }
    if (const auto decoded = analyzer_file_path(document)) {
        const auto source = path_from_utf8(*decoded);
        auto relative = std::filesystem::path();
        auto root_length = 0uz;
        for (const auto& uri : roots) {
            const auto root = analyzer_file_path(document_identity(uri));
            if (!root || root->size() <= root_length) {
                continue;
            }
            const auto candidate = source.lexically_relative(path_from_utf8(*root));
            if (candidate.empty() || candidate.is_absolute() || *candidate.begin() == "..") {
                continue;
            }
            relative = candidate;
            root_length = root->size();
        }
        if (root_length != 0) {
            if (auto path = CanonicalModulePath::from_source_path(path_to_generic_utf8(relative))) {
                return std::move(*path);
            }
        }
    }
    auto path = CanonicalModulePath::from_value(std::format("editor.buffer_{}", ++next_module_id));
    if (!path) {
        std::terminate();
    }
    return std::move(*path);
}

auto AnalyzerDocuments::open(
    std::string_view uri,
    std::string document,
    CanonicalModulePath module_path,
    std::int64_t version,
    std::string text
) noexcept -> bool {
    if (bindings.contains(uri)) {
        return false;
    }
    const auto found =
        documents
            .try_emplace(
                document,
                AnalyzerOpenDocument {.module_path = std::move(module_path), .buffers = {}}
            )
            .first;
    found->second.buffers.emplace(
        std::string(uri),
        AnalyzerBuffer {.version = version, .text = std::move(text)}
    );
    bindings.emplace(uri, std::move(document));
    return true;
}

auto AnalyzerDocuments::change(
    std::string_view uri,
    std::int64_t version,
    std::string text
) noexcept -> bool {
    const auto binding = bindings.find(uri);
    if (binding == bindings.end()) {
        return false;
    }
    auto& buffer = documents.at(binding->second).buffers.at(std::string(uri));
    if (version <= buffer.version) {
        return false;
    }
    buffer = {.version = version, .text = std::move(text)};
    return true;
}

auto AnalyzerDocuments::close(std::string_view uri) noexcept -> std::optional<std::string> {
    const auto binding = bindings.find(uri);
    if (binding == bindings.end()) {
        return std::nullopt;
    }
    auto document = std::move(binding->second);
    const auto found = documents.find(document);
    found->second.buffers.erase(std::string(uri));
    if (found->second.buffers.empty()) {
        documents.erase(found);
    }
    bindings.erase(binding);
    return document;
}

auto AnalyzerDocuments::entries() const noexcept
    -> const std::map<std::string, AnalyzerOpenDocument, std::less<>>& {
    return documents;
}

auto AnalyzerDocuments::contains(std::string_view document) const noexcept -> bool {
    return documents.contains(document);
}

auto AnalyzerDocuments::conflicted(std::string_view document) const noexcept -> bool {
    const auto found = documents.find(document);
    if (found == documents.end()) {
        return false;
    }
    const auto& buffers = found->second.buffers;
    const auto& text = buffers.begin()->second.text;
    return std::ranges::any_of(buffers, [&](const auto& entry) noexcept {
        return entry.second.text != text;
    });
}

auto AnalyzerDocuments::view(std::string_view document, std::string_view context) const noexcept
    -> const AnalyzerBuffer* {
    const auto found = documents.find(document);
    if (found == documents.end()) {
        return nullptr;
    }
    if (const auto buffer = found->second.buffers.find(context);
        buffer != found->second.buffers.end()) {
        return &buffer->second;
    }
    return conflicted(document) ? nullptr : &found->second.buffers.begin()->second;
}

auto AnalyzerDocuments::client_uris(std::string_view context) const noexcept
    -> std::map<std::string, std::string, std::less<>> {
    auto result = std::map<std::string, std::string, std::less<>>();
    for (const auto& [document, state] : documents) {
        if (state.buffers.contains(context)) {
            result.emplace(document, context);
        } else if (!conflicted(document)) {
            result.emplace(document, state.buffers.begin()->first);
        }
    }
    return result;
}
