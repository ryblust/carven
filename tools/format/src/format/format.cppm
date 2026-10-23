module carven:format;

import :diagnostics.diagnostic;
import :frontend.document;
import :source.manager;
import :source.text;
import std;

auto format_source(const SourceManager& sources, SourceID source_id) noexcept
    -> std::expected<std::string, Diagnostics>;

// Reuses complete cached syntax; recovered trees are never formatted.
auto format_document(const ParsedDocument& document) noexcept
    -> std::expected<std::string, Diagnostics>;
