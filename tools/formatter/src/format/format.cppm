module carven:formatter.format;

import :diagnostics.diagnostic;
import :source.manager;
import :source.text;
import std;

auto format_source(const SourceManager& sources, SourceID source_id) noexcept
    -> std::expected<std::string, Diagnostics>;
