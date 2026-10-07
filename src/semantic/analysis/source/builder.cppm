module carven:semantic.analysis.source.builder;

import :frontend.ast.ids;
import :frontend.ast.storage;
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

class ProgramDraft;

// One source construction transaction. Nested source bodies merge into their
// parent; only successful top-level bodies are delivered to the analysis owner.
class SourceObservation final {
public:
    auto record(
        const ProgramDraft& draft,
        SourceSpan location,
        std::optional<SourceSpan> definition,
        std::optional<ConstructionTypeRef> type
    ) noexcept -> void;
    auto expression(
        const ProgramDraft& draft,
        ASTView syntax,
        ASTExprID id,
        ConstructionTypeRef type
    ) noexcept -> void;
    auto merge(SourceObservation&& child) noexcept -> void;

private:
    std::vector<SourceOccurrenceDraft> occurrences;
    friend class SourceAnalysisBuilder;
};

// Owned by the analysis entry and optionally borrowed by ProgramDraft until
// construction/publication ends. Growing records never expose borrowed elements.
// Declaration selections come from semantic lookup metadata; this owner accumulates
// the resulting occurrences.
class SourceAnalysisBuilder final {
public:
    // Repeated records combine selected identity and established type evidence.
    auto declarations() noexcept -> SourceObservation&;
    auto admit_declarations() noexcept -> void;
    auto add_body(SourceObservation&& observations) noexcept -> void;
    auto resolve_types(const TypeResolution& types) noexcept -> void;
    auto finish(bool published) && noexcept -> std::vector<SourceOccurrence>;

private:
    auto add(SourceOccurrenceDraft occurrence) noexcept -> void;
    std::map<std::pair<SourceID, Span>, SourceOccurrenceDraft> occurrences;
    // Declaration construction can demand a body before the nominal gates pass.
    SourceObservation declaration_observations;
    bool declarations_admitted = false;
};
