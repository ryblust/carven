module carven:source.manager.impl;

import :source.location;
import :source.manager;
import :source.text;
import :support.file;
import :support.invariant;
import :support.path;
import std;

SourceManager::SourceRecord::SourceRecord(std::string origin_value, std::string text_value) noexcept
    : origin(std::move(origin_value)),
      text(std::move(text_value)),
      lines(text) {}

auto SourceManager::append_file(std::string_view path) noexcept
    -> std::expected<SourceID, SourceLoadError> {
    if (entries.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("source manager exhausted its 32-bit identity space");
    }
    constexpr auto maximum_size =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    auto file = read_file(path_from_utf8(path), maximum_size);
    if (!file.has_value()) {
        return std::unexpected(
            SourceLoadError {
                .origin = std::string(path),
                .message = std::format("cannot read source file: {}", file.error().code.message()),
            }
        );
    }
    const auto source_id = SourceID::from_index(static_cast<std::uint32_t>(entries.size()));
    entries.emplace_back(std::string(path), std::move(*file));
    return source_id;
}

auto SourceManager::append_virtual(std::string origin, std::string text) noexcept
    -> std::expected<SourceID, SourceLoadError> {
    if (entries.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("source manager exhausted its 32-bit identity space");
    }
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            SourceLoadError {
                .origin = std::move(origin),
                .message = "virtual source exceeds the 4 GiB span limit",
            }
        );
    }
    const auto source_id = SourceID::from_index(static_cast<std::uint32_t>(entries.size()));
    entries.emplace_back(std::move(origin), std::move(text));
    return source_id;
}

auto SourceManager::contains(SourceID source_id) const noexcept -> bool {
    return source_id.index() < entries.size();
}

auto SourceManager::try_view(SourceID source_id) const noexcept -> std::optional<SourceView> {
    if (!contains(source_id)) {
        return std::nullopt;
    }

    const auto& entry = entries[source_id.index()];
    return SourceView {
        .source_id = source_id,
        .text = entry.text,
        .origin = entry.origin,
    };
}

auto SourceManager::view(SourceID source_id) const noexcept -> SourceView {
    const auto result = try_view(source_id);
    if (!result.has_value()) {
        invariant_violation("source lookup used an invalid identity");
    }
    return *result;
}

auto SourceManager::slice(SourceSpan span) const noexcept -> std::string_view {
    return ::slice(view(span.source_id).text, span.span);
}

auto SourceManager::location(SourceSpan span) const noexcept -> SourceLocation {
    if (!contains(span.source_id)) {
        invariant_violation("source location used an invalid source identity");
    }
    return entries[span.source_id.index()].lines.location(span.span.start());
}
