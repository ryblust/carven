module carven:semantic.analysis.source.impl;

import :semantic.analysis.source;
import std;

auto SourceOccurrence::definition() const noexcept -> std::optional<SourceSpan> {
    if (alias) {
        return alias;
    }
    if (external_binding) {
        return external_binding;
    }
    if (entity) {
        if (const auto* source = std::get_if<SourceSpan>(&*entity)) {
            return *source;
        }
    }
    return std::nullopt;
}
