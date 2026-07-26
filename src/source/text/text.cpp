module carven:source.text.impl;

import :source.text;
import :support.invariant;
import std;

auto locate(SourceID source_id, Span span) noexcept -> SourceSpan {
    return {.source_id = source_id, .span = span};
}

auto slice(std::string_view text, Span span) noexcept -> std::string_view {
    const auto found = try_slice(text, span);
    if (!found.has_value()) {
        invariant_violation("source slice lies outside the source text");
    }
    return *found;
}

auto try_slice(std::string_view text, Span span) noexcept -> std::optional<std::string_view> {
    if (span.end() > text.size()) {
        return std::nullopt;
    }
    return text.substr(span.start(), span.size());
}
