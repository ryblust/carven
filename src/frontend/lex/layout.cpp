module carven:frontend.lex.layout.impl;

import :frontend.lex.layout;
import std;

namespace {

auto common_prefix_size(std::string_view left, std::string_view right) noexcept -> std::size_t {
    return static_cast<std::size_t>(std::ranges::mismatch(left, right).in1 - left.begin());
}

} // namespace

StringBlockLayout::StringBlockLayout() noexcept
    : lines {{.offset = 0uz, .indent = {}, .content = false}} {}

auto StringBlockLayout::append(std::string_view spelling, std::size_t byte_count) noexcept -> void {
    auto& line = lines.back();
    if (!line.content && spelling.size() == 1 && (spelling[0] == ' ' || spelling[0] == '\t')) {
        line.indent += spelling;
    } else {
        line.content = true;
    }
    size += byte_count;
}

auto StringBlockLayout::newline() noexcept -> void {
    ++size;
    lines.push_back({.offset = size, .indent = {}, .content = false});
}

auto StringBlockLayout::hole() noexcept -> void {
    lines.back().content = true;
}

auto StringBlockLayout::closing_line_is_blank() const noexcept -> bool {
    return !lines.back().content;
}

auto StringBlockLayout::finish() const noexcept -> std::vector<StringLayoutRemoval> {
    auto common = std::optional<std::string_view>();
    for (auto index = 0uz; index + 1 < lines.size(); ++index) {
        const auto& line = lines[index];
        if (!line.content) {
            continue;
        }
        if (!common) {
            common = line.indent;
        } else {
            *common = common->substr(0, common_prefix_size(*common, line.indent));
        }
    }
    auto removals = std::vector<StringLayoutRemoval>();
    for (auto index = 0uz; index + 1 < lines.size(); ++index) {
        const auto& line = lines[index];
        const auto length = common ? common_prefix_size(*common, line.indent) : line.indent.size();
        if (length != 0) {
            removals.push_back({.offset = line.offset, .length = length});
        }
    }
    const auto tail = lines.back().offset == 0 ? 0uz : lines.back().offset - 1;
    removals.push_back({.offset = tail, .length = size - tail});
    return removals;
}

auto apply_string_layout(
    std::string_view bytes,
    std::size_t offset,
    std::span<const StringLayoutRemoval> removals
) noexcept -> std::string {
    auto result = std::string();
    auto cursor = offset;
    const auto end = offset + bytes.size();
    const auto first = std::ranges::partition_point(removals, [&](const auto& removal) noexcept {
        return removal.offset + removal.length <= offset;
    });
    for (auto entry = first; entry != removals.end(); ++entry) {
        const auto& removal = *entry;
        if (removal.offset >= end) {
            break;
        }
        if (removal.offset + removal.length <= cursor) {
            continue;
        }
        const auto start = std::max(cursor, removal.offset);
        result.append(bytes.substr(cursor - offset, start - cursor));
        cursor = std::min(end, removal.offset + removal.length);
    }
    result.append(bytes.substr(cursor - offset));
    return result;
}
