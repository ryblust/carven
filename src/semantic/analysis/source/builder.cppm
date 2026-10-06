module carven:semantic.analysis.source.builder;

import :semantic.analysis.source;
import :semantic.semir.type;
import :source.text;
import std;

struct SourceOccurrenceDraft final {
    SourceSpan location;
    std::optional<SourceSpan> definition;
    std::optional<ConstructionTypeRef> type;
    std::optional<BuiltinType> builtin_type;
};

// Owned by the analysis entry and optionally borrowed by ProgramDraft until
// construction/publication ends. Growing records never expose borrowed elements.
// Declaration selections come from semantic lookup metadata; this owner accumulates
// the resulting occurrences.
class SourceAnalysisBuilder final {
public:
    auto declare(
        SourceSpan name,
        std::optional<ConstructionTypeRef> type,
        std::optional<BuiltinType> builtin
    ) noexcept -> void;
    auto begin_bodies() noexcept -> void;
    auto add_body(std::span<const SourceOccurrenceDraft> occurrences) noexcept -> void;
    auto resolve_types(const TypeResolution& types) noexcept -> void;
    auto finish(bool published) const noexcept -> std::vector<SourceOccurrence>;

private:
    auto add(SourceOccurrenceDraft occurrence) noexcept -> void;
    std::map<std::pair<SourceID, Span>, SourceOccurrenceDraft> occurrences;
    // Declaration construction can demand a body before the nominal gates pass.
    bool bodies_started = false;
};
