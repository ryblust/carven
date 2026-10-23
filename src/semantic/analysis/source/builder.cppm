module carven:semantic.analysis.source.builder;

import :frontend.ast.ids;
import :frontend.ast.storage;
import :semantic.analysis.source;
import :semantic.semir.type;
import :source.text;
import std;

struct SourceOccurrenceDraft final {
    SourceSpan location;
    std::optional<ConstructionTypeRef> type;
    std::optional<BuiltinType> builtin_type;
    std::optional<SourceEntity> entity;
    std::optional<SourceSpan> alias;
    std::string type_description;
    std::optional<SourceCallableInformation> callable;
    std::optional<CallableID> call_target;
    std::string documentation;
    std::optional<SourceSpan> external_binding;
};

class ProgramDraft;
class AnalysisCatalogView;

// One source construction transaction. Nested source bodies merge into their
// parent. Ordinary analysis admits successful top-level bodies; source queries
// can retain detached observations from failed bodies.
class SourceObservation final {
public:
    auto record(
        const ProgramDraft& draft,
        SourceSpan location,
        std::optional<SourceEntity> entity,
        std::optional<ConstructionTypeRef> type,
        std::optional<SourceSpan> alias = std::nullopt,
        std::optional<CallableID> target = std::nullopt
    ) noexcept -> void;

    auto expression(
        const ProgramDraft& draft,
        ASTView syntax,
        ASTExprID id,
        ConstructionTypeRef type
    ) noexcept -> void;

    auto call(
        const ProgramDraft& draft,
        ASTView syntax,
        ASTExprID callee,
        ConstructionTypeRef type,
        std::optional<CallableID> target
    ) noexcept -> void;

    auto external(
        SourceSpan location,
        std::string path,
        std::string documentation,
        std::optional<SourceSpan> binding = std::nullopt
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
    explicit SourceAnalysisBuilder(SourceCompletionRequest completion, bool source_query) noexcept;
    // Captured before solving: generic callable shape extraction can intern
    // substituted construction types and failure terms in the same draft.
    auto describe(ProgramDraft& draft, const AnalysisCatalogView& catalog) noexcept -> void;
    auto closure(CallableID callable, SourceSpan source) noexcept -> void;
    auto is_source_query() const noexcept -> bool;
    auto excludes(SourceSpan location) const noexcept -> bool;
    auto requests_completion(SourceSpan location) const noexcept -> bool;
    auto complete(
        std::span<const SourceCompletionCandidate> candidates,
        SourceCompletionContext context = SourceCompletionContext::Name
    ) const noexcept -> void;

    // Repeated records combine selected identity and established type evidence.
    auto declarations() noexcept -> SourceObservation&;
    auto admit_declarations() noexcept -> void;
    auto discard() noexcept -> void;
    auto add_body(SourceObservation&& observations) noexcept -> void;
    auto resolve_types(const TypeResolution& types) noexcept -> void;
    auto complete_construction() noexcept -> void;
    auto construction_complete() const noexcept -> bool;
    auto finish(bool published) && noexcept -> std::vector<SourceOccurrence>;

private:
    auto add(SourceOccurrenceDraft occurrence) noexcept -> void;
    auto callable_source(
        const ProgramDraft& draft,
        const AnalysisCatalogView& catalog,
        CallableID callable
    ) const noexcept -> std::optional<SourceSpan>;
    std::map<CallableID, SourceSpan> closures;
    std::map<std::pair<SourceID, Span>, SourceOccurrenceDraft> occurrences;
    // Declaration construction can demand a body before the nominal gates pass.
    SourceObservation declaration_observations;
    bool declarations_admitted = false;
    bool construction_completed = false;
    SourceCompletionRequest completion;
    bool query_only;
};
