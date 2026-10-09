module carven:source.document.impl;

import :source.document;
import :source.manager;
import :source.text;
import std;

SourceDocument::SourceDocument(SourceManager sources, SourceID id) noexcept
    : source_manager(std::move(sources)),
      source_id(id) {}

auto SourceDocument::create(std::string document, std::string text) noexcept
    -> std::expected<std::shared_ptr<const SourceDocument>, SourceLoadError> {
    auto sources = SourceManager();
    const auto id = sources.append_virtual(std::move(document), std::move(text));
    if (!id) {
        return std::unexpected(id.error());
    }
    return std::shared_ptr<const SourceDocument>(new SourceDocument(std::move(sources), *id));
}

auto SourceDocument::source() const noexcept -> SourceView {
    return source_manager.view(source_id);
}

auto SourceDocument::sources() const noexcept -> const SourceManager& {
    return source_manager;
}

auto SourceDocument::retain(const SourceManager& sources, SourceID source) noexcept
    -> std::shared_ptr<const SourceDocument> {
    auto retained = SourceManager();
    const auto identity = retained.append_shared(sources, source);
    return std::shared_ptr<const SourceDocument>(new SourceDocument(std::move(retained), identity));
}
