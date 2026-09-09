module carven:source.provenance;

import :source.location;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import std;

class ProgramSourceSnapshot final {
public:
    ProgramSourceSnapshot(const ProgramSourceSnapshot&) = delete;
    ProgramSourceSnapshot(ProgramSourceSnapshot&&) = default;
    ~ProgramSourceSnapshot() = default;

    auto operator=(const ProgramSourceSnapshot&) -> ProgramSourceSnapshot& = delete;
    auto operator=(ProgramSourceSnapshot&&) -> ProgramSourceSnapshot& = default;

    auto manager_source_id() const noexcept -> SourceID;
    auto display_origin() const noexcept -> std::string_view;
    auto text() const noexcept -> std::string_view;
    auto size() const noexcept -> std::size_t;
    auto slice(Span span) const noexcept -> std::string_view;
    auto location(Span span) const noexcept -> SourceLocation;

private:
    explicit ProgramSourceSnapshot(SourceView source) noexcept;

    SourceID source_manager_id;
    std::string display_origin_text;
    std::string source_text;
    LineIndex line_index;

    friend class CompilationProvenanceBuilder;
};

struct ProgramSourceOrigin final {
    ProgramSourceID source_id;
    Span span;
};

enum class ProgramExpansionReason {
    ImplicitConversion,
    ControlMerge,
    EvaluationTemporary,
    FailureTransport,
    CallableAdoption,
    SyntheticControl,
};

struct ProgramExpansionOrigin final {
    ProgramOriginID parent_origin_id;
    ProgramExpansionReason reason;
};

using ProgramOriginValue = std::variant<ProgramSourceOrigin, ProgramExpansionOrigin>;

struct ProgramOrigin final {
    ProgramOriginValue value;
};

struct ProgramModule final {
    ProgramSourceID source_id;
    CanonicalModulePath path;
};

class CompilationProvenance;
class CompilationProvenanceAppender;
class CompilationProvenanceBuilder;
class CompilationProvenanceReader;
class CompilationProvenanceView;

class CompilationProvenanceStorage final {
    CompilationProvenanceStorage() noexcept;
    CompilationProvenanceStorage(const CompilationProvenanceStorage&) = delete;
    CompilationProvenanceStorage(CompilationProvenanceStorage&&) noexcept = default;
    ~CompilationProvenanceStorage() = default;

    auto operator=(const CompilationProvenanceStorage&) -> CompilationProvenanceStorage& = delete;
    auto operator=(CompilationProvenanceStorage&&) -> CompilationProvenanceStorage& = delete;

    auto source_id_at(std::size_t index) const noexcept -> ProgramSourceID;
    auto module_id_at(std::size_t index) const noexcept -> ProgramModuleID;
    auto spelling_id_at(std::size_t index) const noexcept -> ProgramSpellingID;
    auto origin_id_at(std::size_t index) const noexcept -> ProgramOriginID;
    auto append_source(ProgramSourceSnapshot source) noexcept -> ProgramSourceID;
    auto append_module(ProgramModule module_record) noexcept -> ProgramModuleID;
    auto append_spelling(std::string spelling) noexcept -> ProgramSpellingID;
    auto append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID;
    auto contains(ProgramSourceID id) const noexcept -> bool;
    auto contains(ProgramModuleID id) const noexcept -> bool;
    auto contains(ProgramSpellingID id) const noexcept -> bool;
    auto contains(ProgramOriginID id) const noexcept -> bool;


    auto source_origin(ProgramOriginID id) const noexcept -> ProgramSourceOrigin;

    ProvenanceIdentity provenance_identity;
    std::vector<ProgramSourceSnapshot> sources;
    std::vector<ProgramModule> modules;
    std::vector<std::string> spellings;
    std::vector<ProgramOrigin> origins;

    friend class CompilationProvenance;
    friend class CompilationProvenanceAppender;
    friend class CompilationProvenanceBuilder;
    friend class CompilationProvenanceReader;
    friend class CompilationProvenanceView;
};

class CompilationProvenance final {
public:
    CompilationProvenance(const CompilationProvenance&) = delete;
    CompilationProvenance(CompilationProvenance&&) noexcept = default;
    ~CompilationProvenance() = default;

    auto operator=(const CompilationProvenance&) -> CompilationProvenance& = delete;
    auto operator=(CompilationProvenance&&) -> CompilationProvenance& = delete;

    auto view() const noexcept -> CompilationProvenanceView;

private:
    explicit CompilationProvenance(CompilationProvenanceStorage storage) noexcept;

    CompilationProvenanceStorage storage;

    friend class CompilationProvenanceBuilder;
    friend class CompilationProvenanceAppender;
    friend class CompilationProvenanceView;
};

class CompilationProvenanceView final {
public:
    auto identity() const noexcept -> ProvenanceIdentity;
    auto contains(ProgramSourceID id) const noexcept -> bool;
    auto contains(ProgramModuleID id) const noexcept -> bool;
    auto contains(ProgramSpellingID id) const noexcept -> bool;
    auto contains(ProgramOriginID id) const noexcept -> bool;
    auto source_id_at(std::size_t index) const noexcept -> ProgramSourceID;
    auto module_id_at(std::size_t index) const noexcept -> ProgramModuleID;
    auto spelling_id_at(std::size_t index) const noexcept -> ProgramSpellingID;
    auto origin_id_at(std::size_t index) const noexcept -> ProgramOriginID;
    auto source_snapshot(ProgramSourceID source_id) const noexcept -> const ProgramSourceSnapshot&;
    auto module_record(ProgramModuleID module_id) const noexcept -> const ProgramModule&;
    auto find_program_module(const CanonicalModulePath& path) const noexcept
        -> std::optional<ProgramModuleID>;
    auto spelling(ProgramSpellingID spelling_id) const noexcept -> std::string_view;
    auto origin(ProgramOriginID origin_id) const noexcept -> const ProgramOrigin&;
    auto source_snapshots() const noexcept -> std::span<const ProgramSourceSnapshot>;
    auto module_records() const noexcept -> std::span<const ProgramModule>;
    auto spellings() const noexcept -> std::span<const std::string>;
    auto origins() const noexcept -> std::span<const ProgramOrigin>;
    auto source_origin(ProgramOriginID origin_id) const noexcept -> ProgramSourceOrigin;
    auto source_span(ProgramOriginID origin_id) const noexcept -> SourceSpan;
    auto slice(ProgramOriginID origin_id) const noexcept -> std::string_view;
    auto location(ProgramOriginID origin_id) const noexcept -> SourceLocation;

private:
    explicit CompilationProvenanceView(const CompilationProvenanceStorage& storage) noexcept;

    const CompilationProvenanceStorage& provenance_storage;

    friend class CompilationProvenance;
    friend class CompilationProvenanceAppender;
    friend class CompilationProvenanceBuilder;
    friend class CompilationProvenanceReader;
};

// A construction-time reader deliberately returns only values. It may observe
// storage that is still growing, so exposing references, spans, or string views
// here would let callers retain borrows across a reallocation.
class CompilationProvenanceReader final {
public:
    auto identity() const noexcept -> ProvenanceIdentity;
    auto contains(ProgramSourceID id) const noexcept -> bool;
    auto contains(ProgramModuleID id) const noexcept -> bool;
    auto contains(ProgramSpellingID id) const noexcept -> bool;
    auto contains(ProgramOriginID id) const noexcept -> bool;
    auto source_id_at(std::size_t index) const noexcept -> ProgramSourceID;
    auto module_id_at(std::size_t index) const noexcept -> ProgramModuleID;
    auto spelling_id_at(std::size_t index) const noexcept -> ProgramSpellingID;
    auto origin_id_at(std::size_t index) const noexcept -> ProgramOriginID;
    auto source_count() const noexcept -> std::size_t;
    auto module_count() const noexcept -> std::size_t;
    auto spelling_count() const noexcept -> std::size_t;
    auto origin_count() const noexcept -> std::size_t;
    auto source_manager_id(ProgramSourceID source_id) const noexcept -> SourceID;
    auto source_display_origin_copy(ProgramSourceID source_id) const noexcept -> std::string;
    auto source_size(ProgramSourceID source_id) const noexcept -> std::size_t;
    auto source_slice_copy(ProgramSourceID source_id, Span span) const noexcept -> std::string;
    auto source_location(ProgramSourceID source_id, Span span) const noexcept -> SourceLocation;
    auto module_source(ProgramModuleID module_id) const noexcept -> ProgramSourceID;
    auto module_path_copy(ProgramModuleID module_id) const noexcept -> CanonicalModulePath;
    auto find_program_module(const CanonicalModulePath& path) const noexcept
        -> std::optional<ProgramModuleID>;
    auto spelling_copy(ProgramSpellingID spelling_id) const noexcept -> std::string;
    auto origin_copy(ProgramOriginID origin_id) const noexcept -> ProgramOrigin;
    auto source_origin(ProgramOriginID origin_id) const noexcept -> ProgramSourceOrigin;
    auto source_span(ProgramOriginID origin_id) const noexcept -> SourceSpan;
    auto slice_copy(ProgramOriginID origin_id) const noexcept -> std::string;
    auto location(ProgramOriginID origin_id) const noexcept -> SourceLocation;

private:
    explicit CompilationProvenanceReader(const CompilationProvenanceStorage& storage) noexcept;

    const CompilationProvenanceStorage& provenance_storage;

    friend class CompilationProvenanceAppender;
    friend class CompilationProvenanceBuilder;
};

class CompilationProvenanceAppender final {
public:
    explicit CompilationProvenanceAppender(CompilationProvenance&& provenance) noexcept;
    CompilationProvenanceAppender(const CompilationProvenanceAppender&) = delete;
    CompilationProvenanceAppender(CompilationProvenanceAppender&&) noexcept = default;
    ~CompilationProvenanceAppender() = default;

    auto operator=(const CompilationProvenanceAppender&) -> CompilationProvenanceAppender& = delete;
    auto operator=(CompilationProvenanceAppender&&) -> CompilationProvenanceAppender& = delete;

    auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID;
    auto append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID;
    auto module_count() const noexcept -> std::size_t;
    auto reader() const noexcept -> CompilationProvenanceReader;
    auto finish() && noexcept -> CompilationProvenance;

private:
    CompilationProvenanceStorage storage;
    std::flat_map<std::string, ProgramSpellingID, std::less<>> spelling_ids_by_value;
};

class CompilationProvenanceBuilder final {
public:
    CompilationProvenanceBuilder() = default;
    CompilationProvenanceBuilder(const CompilationProvenanceBuilder&) = delete;
    CompilationProvenanceBuilder(CompilationProvenanceBuilder&&) noexcept = default;
    ~CompilationProvenanceBuilder() = default;

    auto operator=(const CompilationProvenanceBuilder&) -> CompilationProvenanceBuilder& = delete;
    auto operator=(CompilationProvenanceBuilder&&) -> CompilationProvenanceBuilder& = delete;

    auto intern_source_snapshot(SourceView source) noexcept -> ProgramSourceID;
    auto append_module(ProgramModule program_module) noexcept -> ProgramModuleID;
    auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID;
    auto append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID;
    auto find_source_snapshot(SourceID source_id) const noexcept -> std::optional<ProgramSourceID>;
    auto find_program_module(const CanonicalModulePath& path) const noexcept
        -> std::optional<ProgramModuleID>;
    auto identity() const noexcept -> ProvenanceIdentity;
    auto source_id_at(std::size_t index) const noexcept -> ProgramSourceID;
    auto module_id_at(std::size_t index) const noexcept -> ProgramModuleID;
    auto spelling_id_at(std::size_t index) const noexcept -> ProgramSpellingID;
    auto origin_id_at(std::size_t index) const noexcept -> ProgramOriginID;
    auto module_count() const noexcept -> std::size_t;
    auto reader() const noexcept -> CompilationProvenanceReader;
    auto finish() && noexcept -> CompilationProvenance;

private:
    CompilationProvenanceStorage storage;
    std::flat_map<SourceID, ProgramSourceID> program_source_ids_by_source_id;
    std::flat_map<CanonicalModulePath, ProgramModuleID> module_ids_by_path;
    std::flat_map<std::string, ProgramSpellingID, std::less<>> spelling_ids_by_value;
};
