module carven:source.document;

import :source.manager;
import :source.text;
import std;

class SourceDocument final {
public:
    static auto create(std::string document, std::string text) noexcept
        -> std::expected<std::shared_ptr<const SourceDocument>, SourceLoadError>;

    static auto retain(const SourceManager& sources, SourceID source) noexcept
        -> std::shared_ptr<const SourceDocument>;

    auto source() const noexcept -> SourceView;
    auto sources() const noexcept -> const SourceManager&;

private:
    SourceDocument(SourceManager sources, SourceID id) noexcept;

    SourceManager source_manager;
    SourceID source_id;
};
