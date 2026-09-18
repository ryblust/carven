module carven:source.location.impl;

import :source.location;
import :support.invariant;
import std;

LineIndex::LineIndex(std::string_view text) noexcept
    : text_size(
          static_cast<std::uint32_t>(
              std::min<std::size_t>(text.size(), std::numeric_limits<std::uint32_t>::max())
          )
      ) {
    line_starts.reserve(text.size() / 40 + 1);
    line_starts.push_back(0);

    for (auto index = 0uz; index < text_size; ++index) {
        if (text[index] == '\n') {
            line_starts.push_back(static_cast<std::uint32_t>(index + 1));
        }
    }
}

auto LineIndex::location(std::uint32_t offset) const noexcept -> SourceLocation {
    offset = std::min(offset, text_size);
    const auto iter = std::ranges::upper_bound(line_starts, offset);
    const auto index = static_cast<std::uint32_t>(iter - line_starts.begin() - 1);
    return {
        .line = index + 1,
        .column = offset - line_starts[index] + 1,
    };
}

auto LineIndex::line_span(std::uint32_t line) const noexcept -> Span {
    if (line == 0 || line > line_starts.size()) {
        invariant_violation("source line lookup used an invalid line number");
    }
    return Span::from_bounds(
        line_starts[line - 1],
        line < line_starts.size() ? line_starts[line] : text_size
    );
}
