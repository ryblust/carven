module carven:semantic.analysis.source.builder.impl;

import :semantic.analysis.source;
import :semantic.analysis.source.builder;
import std;

auto SourceAnalysisBuilder::declare(
    ProgramOriginID origin,
    SourceSpan name,
    std::optional<ConstructionTypeRef> type,
    std::optional<BuiltinType> builtin
) noexcept -> void {
    if (name.span.empty()) {
        return;
    }
    declarations.emplace(origin, name);
    add({.location = name, .definition = name, .type = type, .builtin_type = builtin});
}

auto SourceAnalysisBuilder::definition(ProgramOriginID origin) const noexcept
    -> std::optional<SourceSpan> {
    const auto found = declarations.find(origin);
    return found == declarations.end() ? std::nullopt : std::optional(found->second);
}

auto SourceAnalysisBuilder::begin_bodies() noexcept -> void {
    bodies_started = true;
}

auto SourceAnalysisBuilder::add(SourceOccurrenceDraft occurrence) noexcept -> void {
    const auto key = std::pair(occurrence.location.source_id, occurrence.location.span);
    const auto [found, inserted] = occurrences.try_emplace(key, occurrence);
    if (inserted) {
        return;
    }
    if (occurrence.definition) {
        found->second.definition = occurrence.definition;
    }
    if (occurrence.type) {
        found->second.type = occurrence.type;
        found->second.builtin_type = occurrence.builtin_type;
    }
}

auto SourceAnalysisBuilder::add_body(std::vector<SourceOccurrenceDraft> body) noexcept -> void {
    for (auto& occurrence : body) {
        add(std::move(occurrence));
    }
}

auto SourceAnalysisBuilder::resolve_types(const TypeResolution& types) noexcept -> void {
    for (auto& [key, occurrence] : occurrences) {
        if (occurrence.type) {
            occurrence.type = types.resolve(*occurrence.type);
        }
    }
}

auto SourceAnalysisBuilder::finish(bool published) const noexcept -> std::vector<SourceOccurrence> {
    auto result = std::vector<SourceOccurrence>();
    if (!bodies_started) {
        return result;
    }
    result.reserve(occurrences.size());
    for (const auto& [key, occurrence] : occurrences) {
        auto type = std::optional<SourceType>();
        const auto* resolved =
            published && occurrence.type ? std::get_if<TypeID>(&*occurrence.type) : nullptr;
        if (resolved != nullptr) {
            type = *resolved;
        } else if (occurrence.builtin_type) {
            type = *occurrence.builtin_type;
        }
        result.push_back(
            {.location = occurrence.location, .definition = occurrence.definition, .type = type}
        );
    }
    return result;
}
