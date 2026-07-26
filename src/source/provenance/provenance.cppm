module carven:source.provenance;

import :source.location;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :support.id_table;
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

struct ProgramOrigin final {
    ProgramSourceID source_id;
    Span span;
    std::optional<ProgramOriginID> parent_origin_id;
};

struct ProgramModule final {
    ProgramSourceID source_id;
    CanonicalModulePath path;
};

class CompilationProvenance;
class CompilationProvenanceAppender;
class CompilationProvenanceBuilder;
class CompilationProvenanceView;

class CompilationProvenanceStorage final {
    CompilationProvenanceStorage() = default;

    IDTable<ProgramSourceSnapshot, ProgramSourceID> sources;
    IDTable<ProgramModule, ProgramModuleID> modules;
    IDTable<std::string, ProgramSpellingID> spellings;
    IDTable<ProgramOrigin, ProgramOriginID> origins;

    friend class CompilationProvenance;
    friend class CompilationProvenanceAppender;
    friend class CompilationProvenanceBuilder;
    friend class CompilationProvenanceView;
};

class CompilationProvenance final {
public:
    CompilationProvenance(const CompilationProvenance&) = delete;
    CompilationProvenance(CompilationProvenance&&) = default;
    ~CompilationProvenance() = default;

    auto operator=(const CompilationProvenance&) -> CompilationProvenance& = delete;
    auto operator=(CompilationProvenance&&) -> CompilationProvenance& = default;

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
    auto source_snapshot(ProgramSourceID source_id) const noexcept -> const ProgramSourceSnapshot&;
    auto find_source_snapshot(SourceID source_id) const noexcept -> std::optional<ProgramSourceID>;
    auto module_record(ProgramModuleID module_id) const noexcept -> const ProgramModule&;
    auto find_program_module(const CanonicalModulePath& path) const noexcept
        -> std::optional<ProgramModuleID>;
    auto spelling(ProgramSpellingID spelling_id) const noexcept -> std::string_view;
    auto origin(ProgramOriginID origin_id) const noexcept -> const ProgramOrigin&;
    auto source_snapshots() const noexcept -> std::span<const ProgramSourceSnapshot>;
    auto module_records() const noexcept -> std::span<const ProgramModule>;
    auto spellings() const noexcept -> std::span<const std::string>;
    auto origins() const noexcept -> std::span<const ProgramOrigin>;
    auto source_span(ProgramOriginID origin_id) const noexcept -> SourceSpan;
    auto slice(ProgramOriginID origin_id) const noexcept -> std::string_view;
    auto location(ProgramOriginID origin_id) const noexcept -> SourceLocation;

private:
    explicit CompilationProvenanceView(const CompilationProvenanceStorage& storage) noexcept;

    const CompilationProvenanceStorage* provenance_storage;

    friend class CompilationProvenance;
    friend class CompilationProvenanceAppender;
    friend class CompilationProvenanceBuilder;
};

class CompilationProvenanceAppender final {
public:
    explicit CompilationProvenanceAppender(CompilationProvenance&& provenance) noexcept;
    CompilationProvenanceAppender(const CompilationProvenanceAppender&) = delete;
    CompilationProvenanceAppender(CompilationProvenanceAppender&&) = default;
    ~CompilationProvenanceAppender() = default;

    auto operator=(const CompilationProvenanceAppender&) -> CompilationProvenanceAppender& = delete;
    auto operator=(CompilationProvenanceAppender&&) noexcept
        -> CompilationProvenanceAppender& = default;

    auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID;
    auto append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID;
    auto module_count() const noexcept -> std::size_t;
    auto view() const noexcept -> CompilationProvenanceView;
    auto finish() && noexcept -> CompilationProvenance;

private:
    CompilationProvenanceStorage storage;
    std::flat_map<std::string, ProgramSpellingID, std::less<>> spelling_ids_by_value;
};

class CompilationProvenanceBuilder final {
public:
    CompilationProvenanceBuilder() = default;
    CompilationProvenanceBuilder(const CompilationProvenanceBuilder&) = delete;
    CompilationProvenanceBuilder(CompilationProvenanceBuilder&&) = default;
    ~CompilationProvenanceBuilder() = default;

    auto operator=(const CompilationProvenanceBuilder&) -> CompilationProvenanceBuilder& = delete;
    auto operator=(CompilationProvenanceBuilder&&) -> CompilationProvenanceBuilder& = default;

    auto intern_source_snapshot(SourceView source) noexcept -> ProgramSourceID;
    auto append_module(ProgramModule program_module) noexcept -> ProgramModuleID;
    auto intern_spelling(std::string_view spelling) noexcept -> ProgramSpellingID;
    auto append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID;
    auto source_snapshot(ProgramSourceID source_id) const noexcept -> const ProgramSourceSnapshot&;
    auto find_source_snapshot(SourceID source_id) const noexcept -> std::optional<ProgramSourceID>;
    auto module_record(ProgramModuleID module_id) const noexcept -> const ProgramModule&;
    auto find_program_module(const CanonicalModulePath& path) const noexcept
        -> std::optional<ProgramModuleID>;
    auto spelling(ProgramSpellingID spelling_id) const noexcept -> std::string_view;
    auto origin(ProgramOriginID origin_id) const noexcept -> const ProgramOrigin&;
    auto module_records() const noexcept -> std::span<const ProgramModule>;
    auto origins() const noexcept -> std::span<const ProgramOrigin>;
    auto module_count() const noexcept -> std::size_t;
    auto source_span(ProgramOriginID origin_id) const noexcept -> SourceSpan;
    auto view() const noexcept -> CompilationProvenanceView;
    auto finish() && noexcept -> CompilationProvenance;

private:
    CompilationProvenanceStorage storage;
    std::flat_map<SourceID, ProgramSourceID> program_source_ids_by_source_id;
    std::flat_map<CanonicalModulePath, ProgramModuleID> module_ids_by_path;
    std::flat_map<std::string, ProgramSpellingID, std::less<>> spelling_ids_by_value;
};
