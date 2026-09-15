module carven:graver.format;

import :diagnostics.diagnostic;
import :source.manager;
import :source.text;
import std;

namespace graver {

auto format(const SourceManager& sources, SourceID source_id) noexcept
    -> std::expected<std::string, Diagnostics>;

}
