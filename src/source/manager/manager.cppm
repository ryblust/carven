module carven:source.manager;

import :source.location;
import :source.text;
import std;

class SourceManager final {
public:
    SourceManager() = default;
    SourceManager(const SourceManager&) = delete;
    SourceManager(SourceManager&&) = default;
    ~SourceManager() = default;

    auto operator=(const SourceManager&) -> SourceManager& = delete;
    auto operator=(SourceManager&&) -> SourceManager& = default;

    auto append_file(std::string_view path) noexcept -> std::expected<SourceID, SourceLoadError>;
    auto append_virtual(std::string origin, std::string text) noexcept
        -> std::expected<SourceID, SourceLoadError>;

    auto contains(SourceID source_id) const noexcept -> bool;
    auto try_view(SourceID source_id) const noexcept -> std::optional<SourceView>;
    auto view(SourceID source_id) const noexcept -> SourceView;
    auto slice(SourceSpan span) const noexcept -> std::string_view;
    auto location(SourceSpan span) const noexcept -> SourceLocation;

private:
    struct SourceRecord final {
        std::string origin;
        std::string text;
        LineIndex lines;

        SourceRecord(std::string origin_value, std::string text_value) noexcept;
    };

    std::deque<SourceRecord> entries;
};
