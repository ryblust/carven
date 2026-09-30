module carven:diagnostics.suggestion.impl;

import :diagnostics.suggestion;
import std;

namespace {

// Count an adjacent transposition as one spelling edit.
auto edit_distance(std::string_view left, std::string_view right) noexcept -> std::size_t {
    auto before = std::vector<std::size_t>(right.size() + 1uz);
    auto previous = std::vector<std::size_t>(right.size() + 1uz);
    auto current = std::vector<std::size_t>(right.size() + 1uz);
    std::ranges::iota(previous, 0uz);
    for (auto row = 0uz; row < left.size(); ++row) {
        current[0] = row + 1uz;
        for (auto column = 0uz; column < right.size(); ++column) {
            const auto substitution = previous[column] + (left[row] == right[column] ? 0uz : 1uz);
            current[column + 1uz] =
                std::min({previous[column + 1uz] + 1uz, current[column] + 1uz, substitution});
            if (row > 0uz
                && column > 0uz
                && left[row] == right[column - 1uz]
                && left[row - 1uz] == right[column]) {
                current[column + 1uz] = std::min(current[column + 1uz], before[column - 1uz] + 1uz);
            }
        }
        std::swap(before, previous);
        std::swap(previous, current);
    }
    return previous[right.size()];
}

} // namespace

auto spelling_suggestion(std::string_view name, std::span<const std::string> candidates) noexcept
    -> std::string {
    // Bound suggestions to one edit per three bytes, with a minimum of one.
    const auto limit = std::max(1uz, name.size() / 3uz);
    auto best = std::optional<std::string_view>();
    auto best_distance = limit + 1uz;
    for (const auto& candidate : candidates) {
        if (candidate == name) {
            continue;
        }
        const auto distance = edit_distance(name, candidate);
        if (distance < best_distance || (distance == best_distance && best && candidate < *best)) {
            best = candidate;
            best_distance = distance;
        }
    }
    // Exclude a replacement of the entire spelling.
    return best && best_distance <= limit && best_distance < name.size()
        ? std::format("; did you mean '{}'?", *best)
        : std::string();
}
