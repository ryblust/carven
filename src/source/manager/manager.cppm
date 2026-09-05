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
    class SourceRecord final {
    public:
        SourceRecord(std::string origin_value, std::string text_value) noexcept;

        auto display_origin() const noexcept -> std::string_view;
        auto source_text() const noexcept -> std::string_view;
        auto location(Span span) const noexcept -> SourceLocation;

    private:
        std::string origin;
        std::string text;
        LineIndex lines;
    };

    std::deque<SourceRecord> entries;
};
