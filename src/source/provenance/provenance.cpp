module carven:source.provenance.impl;

import :source.provenance.ids;
import :source.provenance.verify;
import :source.provenance;
import :support.invariant;
import :support.visit;
import std;

ProgramSourceSnapshot::ProgramSourceSnapshot(SourceView source) noexcept
    : source_manager_id(source.source_id),
      display_origin_text(source.origin),
      source_text(source.text),
      line_index(source_text) {}

auto ProgramSourceSnapshot::manager_source_id() const noexcept -> SourceID {
    return source_manager_id;
}

auto ProgramSourceSnapshot::display_origin() const noexcept -> std::string_view {
    return display_origin_text;
}

auto ProgramSourceSnapshot::text() const noexcept -> std::string_view {
    return source_text;
}

auto ProgramSourceSnapshot::size() const noexcept -> std::size_t {
    return source_text.size();
}

auto ProgramSourceSnapshot::slice(Span span) const noexcept -> std::string_view {
    return ::slice(source_text, span);
}

auto ProgramSourceSnapshot::location(Span span) const noexcept -> SourceLocation {
    return line_index.location(span.start());
}

CompilationProvenanceStorage::CompilationProvenanceStorage() noexcept
    : provenance_identity(ProvenanceIdentity::fresh()) {}

auto CompilationProvenanceStorage::source_id_at(std::size_t index) const noexcept
    -> ProgramSourceID {
    if (index >= sources.size()) {
        invariant_violation("program source index is out of bounds");
    }
    return ProgramSourceID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::module_id_at(std::size_t index) const noexcept
    -> ProgramModuleID {
    if (index >= modules.size()) {
        invariant_violation("program module index is out of bounds");
    }
    return ProgramModuleID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    if (index >= spellings.size()) {
        invariant_violation("program spelling index is out of bounds");
    }
    return ProgramSpellingID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::origin_id_at(std::size_t index) const noexcept
    -> ProgramOriginID {
    if (index >= origins.size()) {
        invariant_violation("program origin index is out of bounds");
    }
    return ProgramOriginID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::append_source(ProgramSourceSnapshot source) noexcept
    -> ProgramSourceID {
    if (sources.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program source identity space exhausted");
    }
    sources.push_back(std::move(source));
    return source_id_at(sources.size() - 1uz);
}

auto CompilationProvenanceStorage::append_module(ProgramModule module_record) noexcept
    -> ProgramModuleID {
    if (modules.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program module identity space exhausted");
    }
    modules.push_back(std::move(module_record));
    return module_id_at(modules.size() - 1uz);
}

auto CompilationProvenanceStorage::append_spelling(std::string spelling) noexcept
    -> ProgramSpellingID {
    if (spellings.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program spelling identity space exhausted");
    }
    spellings.push_back(std::move(spelling));
    return spelling_id_at(spellings.size() - 1uz);
}

auto CompilationProvenanceStorage::append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID {
    if (origins.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program origin identity space exhausted");
    }
    std::visit(
        Overloaded {
            [&](const ProgramSourceOrigin& value) noexcept {
                if (!contains(value.source_id)
                    || value.span.end() > sources[value.source_id.index()].size()) {
                    invariant_violation("source program origin is outside its provenance owner");
                }
            },
            [&](const ProgramExpansionOrigin& value) noexcept {
                if (!contains(value.parent_origin_id)) {
                    invariant_violation("expansion program origin has no local parent");
                }
            },
        },
        origin.value
    );
    origins.push_back(origin);
    return origin_id_at(origins.size() - 1uz);
}

auto CompilationProvenanceStorage::contains(ProgramSourceID id) const noexcept -> bool {
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < sources.size();
}

auto CompilationProvenanceStorage::contains(ProgramModuleID id) const noexcept -> bool {
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < modules.size();
}

auto CompilationProvenanceStorage::contains(ProgramSpellingID id) const noexcept -> bool {
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < spellings.size();
}

auto CompilationProvenanceStorage::contains(ProgramOriginID id) const noexcept -> bool {
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < origins.size();
}

CompilationProvenance::CompilationProvenance(CompilationProvenanceStorage storage_value) noexcept
    : storage(std::move(storage_value)) {}

auto CompilationProvenance::view() const noexcept -> CompilationProvenanceView {
    return CompilationProvenanceView(storage);
}

CompilationProvenanceView::CompilationProvenanceView(
    const CompilationProvenanceStorage& storage_value
) noexcept
    : provenance_storage(storage_value) {}

auto CompilationProvenanceView::identity() const noexcept -> ProvenanceIdentity {
    return provenance_storage.provenance_identity;
}

auto CompilationProvenanceView::contains(ProgramSourceID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceView::contains(ProgramModuleID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceView::contains(ProgramSpellingID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceView::contains(ProgramOriginID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceView::source_id_at(std::size_t index) const noexcept -> ProgramSourceID {
    return provenance_storage.source_id_at(index);
}

auto CompilationProvenanceView::module_id_at(std::size_t index) const noexcept -> ProgramModuleID {
    return provenance_storage.module_id_at(index);
}

auto CompilationProvenanceView::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    return provenance_storage.spelling_id_at(index);
}

auto CompilationProvenanceView::origin_id_at(std::size_t index) const noexcept -> ProgramOriginID {
    return provenance_storage.origin_id_at(index);
}

auto CompilationProvenanceView::source_snapshot(ProgramSourceID source_id) const noexcept
    -> const ProgramSourceSnapshot& {
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage.sources[source_id.index()];
}

auto CompilationProvenanceView::module_record(ProgramModuleID module_id) const noexcept
    -> const ProgramModule& {
    if (!contains(module_id)) {
        invariant_violation("program module lookup used a foreign or invalid identity");
    }
    return provenance_storage.modules[module_id.index()];
}

auto CompilationProvenanceView::find_program_module(const CanonicalModulePath& path) const noexcept
    -> std::optional<ProgramModuleID> {
    const auto& modules = provenance_storage.modules;
    const auto found = std::ranges::find(modules, path, &ProgramModule::path);
    if (found == modules.end()) {
        return std::nullopt;
    }
    return module_id_at(static_cast<std::size_t>(found - modules.begin()));
}

auto CompilationProvenanceView::spelling(ProgramSpellingID spelling_id) const noexcept
    -> std::string_view {
    if (!contains(spelling_id)) {
        invariant_violation("program spelling lookup used a foreign or invalid identity");
    }
    return provenance_storage.spellings[spelling_id.index()];
}

auto CompilationProvenanceView::origin(ProgramOriginID origin_id) const noexcept
    -> const ProgramOrigin& {
    if (!contains(origin_id)) {
        invariant_violation("program origin lookup used a foreign or invalid identity");
    }
    return provenance_storage.origins[origin_id.index()];
}

auto CompilationProvenanceView::source_snapshots() const noexcept
    -> std::span<const ProgramSourceSnapshot> {
    return provenance_storage.sources;
}

auto CompilationProvenanceView::module_records() const noexcept -> std::span<const ProgramModule> {
    return provenance_storage.modules;
}

auto CompilationProvenanceView::spellings() const noexcept -> std::span<const std::string> {
    return provenance_storage.spellings;
}

auto CompilationProvenanceView::origins() const noexcept -> std::span<const ProgramOrigin> {
    return provenance_storage.origins;
}

auto CompilationProvenanceView::source_origin(ProgramOriginID id) const noexcept
    -> ProgramSourceOrigin {
    return provenance_storage.source_origin(id);
}

auto CompilationProvenanceView::source_span(ProgramOriginID id) const noexcept -> SourceSpan {
    const auto source = source_origin(id);
    return {
        .source_id = source_snapshot(source.source_id).manager_source_id(),
        .span = source.span
    };
}

auto CompilationProvenanceView::slice(ProgramOriginID origin_id) const noexcept
    -> std::string_view {
    const auto source = source_origin(origin_id);
    return source_snapshot(source.source_id).slice(source.span);
}

auto CompilationProvenanceView::location(ProgramOriginID origin_id) const noexcept
    -> SourceLocation {
    const auto source = source_origin(origin_id);
    return source_snapshot(source.source_id).location(source.span);
}

CompilationProvenanceReader::CompilationProvenanceReader(
    const CompilationProvenanceStorage& storage_value
) noexcept
    : provenance_storage(storage_value) {}

auto CompilationProvenanceReader::identity() const noexcept -> ProvenanceIdentity {
    return provenance_storage.provenance_identity;
}

auto CompilationProvenanceReader::contains(ProgramSourceID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceReader::contains(ProgramModuleID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceReader::contains(ProgramSpellingID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceReader::contains(ProgramOriginID id) const noexcept -> bool {
    return provenance_storage.contains(id);
}

auto CompilationProvenanceReader::source_id_at(std::size_t index) const noexcept
    -> ProgramSourceID {
    return provenance_storage.source_id_at(index);
}

auto CompilationProvenanceReader::module_id_at(std::size_t index) const noexcept
    -> ProgramModuleID {
    return provenance_storage.module_id_at(index);
}

auto CompilationProvenanceReader::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    return provenance_storage.spelling_id_at(index);
}

auto CompilationProvenanceReader::origin_id_at(std::size_t index) const noexcept
    -> ProgramOriginID {
    return provenance_storage.origin_id_at(index);
}

auto CompilationProvenanceReader::source_count() const noexcept -> std::size_t {
    return provenance_storage.sources.size();
}

auto CompilationProvenanceReader::module_count() const noexcept -> std::size_t {
    return provenance_storage.modules.size();
}

auto CompilationProvenanceReader::spelling_count() const noexcept -> std::size_t {
    return provenance_storage.spellings.size();
}

auto CompilationProvenanceReader::origin_count() const noexcept -> std::size_t {
    return provenance_storage.origins.size();
}

auto CompilationProvenanceReader::source_manager_id(ProgramSourceID source_id) const noexcept
    -> SourceID {
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage.sources[source_id.index()].manager_source_id();
}

auto CompilationProvenanceReader::source_display_origin_copy(
    ProgramSourceID source_id
) const noexcept -> std::string {
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return std::string(provenance_storage.sources[source_id.index()].display_origin());
}

auto CompilationProvenanceReader::source_size(ProgramSourceID source_id) const noexcept
    -> std::size_t {
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage.sources[source_id.index()].size();
}

auto CompilationProvenanceReader::source_slice_copy(
    ProgramSourceID source_id,
    Span span
) const noexcept -> std::string {
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return std::string(provenance_storage.sources[source_id.index()].slice(span));
}

auto CompilationProvenanceReader::source_location(
    ProgramSourceID source_id,
    Span span
) const noexcept -> SourceLocation {
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage.sources[source_id.index()].location(span);
}

auto CompilationProvenanceReader::module_source(ProgramModuleID module_id) const noexcept
    -> ProgramSourceID {
    if (!contains(module_id)) {
        invariant_violation("program module lookup used a foreign or invalid identity");
    }
    return provenance_storage.modules[module_id.index()].source_id;
}

auto CompilationProvenanceReader::module_path_copy(ProgramModuleID module_id) const noexcept
    -> CanonicalModulePath {
    if (!contains(module_id)) {
        invariant_violation("program module lookup used a foreign or invalid identity");
    }
    return provenance_storage.modules[module_id.index()].path;
}

auto CompilationProvenanceReader::find_program_module(
    const CanonicalModulePath& path
) const noexcept -> std::optional<ProgramModuleID> {
    const auto& modules = provenance_storage.modules;
    const auto found = std::ranges::find(modules, path, &ProgramModule::path);
    if (found == modules.end()) {
        return std::nullopt;
    }
    return module_id_at(static_cast<std::size_t>(found - modules.begin()));
}

auto CompilationProvenanceReader::spelling_copy(ProgramSpellingID spelling_id) const noexcept
    -> std::string {
    if (!contains(spelling_id)) {
        invariant_violation("program spelling lookup used a foreign or invalid identity");
    }
    return provenance_storage.spellings[spelling_id.index()];
}

auto CompilationProvenanceReader::origin_copy(ProgramOriginID origin_id) const noexcept
    -> ProgramOrigin {
    if (!contains(origin_id)) {
        invariant_violation("program origin lookup used a foreign or invalid identity");
    }
    return provenance_storage.origins[origin_id.index()];
}

auto CompilationProvenanceReader::source_origin(ProgramOriginID id) const noexcept
    -> ProgramSourceOrigin {
    return provenance_storage.source_origin(id);
}

auto CompilationProvenanceReader::source_span(ProgramOriginID id) const noexcept -> SourceSpan {
    const auto source = source_origin(id);
    return {.source_id = source_manager_id(source.source_id), .span = source.span};
}

auto CompilationProvenanceReader::slice_copy(ProgramOriginID origin_id) const noexcept
    -> std::string {
    const auto source = source_origin(origin_id);
    return source_slice_copy(source.source_id, source.span);
}

auto CompilationProvenanceReader::location(ProgramOriginID origin_id) const noexcept
    -> SourceLocation {
    const auto source = source_origin(origin_id);
    return source_location(source.source_id, source.span);
}

CompilationProvenanceAppender::CompilationProvenanceAppender(
    CompilationProvenance&& provenance
) noexcept
    : storage(std::move(provenance.storage)) {
    for (const auto& [index, spelling] : std::views::enumerate(storage.spellings)) {
        const auto spelling_id = storage.spelling_id_at(static_cast<std::size_t>(index));
        spelling_ids_by_value.emplace(spelling, spelling_id);
    }
}

auto CompilationProvenanceAppender::intern_spelling(std::string_view spelling) noexcept
    -> ProgramSpellingID {
    if (const auto found = spelling_ids_by_value.find(spelling);
        found != spelling_ids_by_value.end()) {
        return found->second;
    }
    const auto spelling_id = storage.append_spelling(std::string(spelling));
    spelling_ids_by_value.emplace(storage.spellings[spelling_id.index()], spelling_id);
    return spelling_id;
}

auto CompilationProvenanceAppender::append_origin(ProgramOrigin origin) noexcept
    -> ProgramOriginID {
    return storage.append_origin(origin);
}

auto CompilationProvenanceAppender::module_count() const noexcept -> std::size_t {
    return storage.modules.size();
}

auto CompilationProvenanceAppender::reader() const noexcept -> CompilationProvenanceReader {
    return CompilationProvenanceReader(storage);
}

auto CompilationProvenanceAppender::finish() && noexcept -> CompilationProvenance {
    auto provenance = CompilationProvenance(std::move(storage));
    const auto verification = verify_compilation_provenance(provenance.view());
    if (!verification.has_value()) {
        invariant_violation(verification.error().message);
    }
    return provenance;
}

auto CompilationProvenanceBuilder::append_module(ProgramModule program_module) noexcept
    -> ProgramModuleID {
    if (!storage.contains(program_module.source_id)) {
        invariant_violation("program module references an unknown source snapshot");
    }
    if (module_ids_by_path.contains(program_module.path)) {
        invariant_violation("program module path was added more than once");
    }
    if (std::ranges::contains(
            storage.modules,
            program_module.source_id,
            &ProgramModule::source_id
        )) {
        invariant_violation("program source snapshot was assigned to more than one module");
    }

    const auto module_id = storage.append_module(std::move(program_module));
    module_ids_by_path.emplace(storage.modules[module_id.index()].path, module_id);
    return module_id;
}

auto CompilationProvenanceBuilder::intern_source_snapshot(SourceView source) noexcept
    -> ProgramSourceID {
    if (source.text.size() > std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program source snapshot exceeds the 4 GiB span limit");
    }
    if (const auto found = program_source_ids_by_source_id.find(source.source_id);
        found != program_source_ids_by_source_id.end()) {
        const auto& existing = storage.sources[found->second.index()];
        if (existing.display_origin() != source.origin
            || existing.size() != source.text.size()
            || existing.slice(Span::from_bounds(0, static_cast<std::uint32_t>(source.text.size())))
                != source.text) {
            invariant_violation("program source identity was rebound to another snapshot");
        }
        return found->second;
    }

    const auto source_id = storage.append_source(ProgramSourceSnapshot(source));
    program_source_ids_by_source_id.emplace(source.source_id, source_id);
    return source_id;
}

auto CompilationProvenanceBuilder::intern_spelling(std::string_view spelling) noexcept
    -> ProgramSpellingID {
    if (const auto found = spelling_ids_by_value.find(spelling);
        found != spelling_ids_by_value.end()) {
        return found->second;
    }
    const auto spelling_id = storage.append_spelling(std::string(spelling));
    spelling_ids_by_value.emplace(storage.spellings[spelling_id.index()], spelling_id);
    return spelling_id;
}

auto CompilationProvenanceBuilder::append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID {
    return storage.append_origin(origin);
}

auto CompilationProvenanceBuilder::find_source_snapshot(SourceID source_id) const noexcept
    -> std::optional<ProgramSourceID> {
    const auto found = program_source_ids_by_source_id.find(source_id);
    if (found == program_source_ids_by_source_id.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto CompilationProvenanceBuilder::find_program_module(
    const CanonicalModulePath& path
) const noexcept -> std::optional<ProgramModuleID> {
    const auto found = module_ids_by_path.find(path);
    if (found == module_ids_by_path.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto CompilationProvenanceBuilder::identity() const noexcept -> ProvenanceIdentity {
    return storage.provenance_identity;
}

auto CompilationProvenanceBuilder::source_id_at(std::size_t index) const noexcept
    -> ProgramSourceID {
    return storage.source_id_at(index);
}

auto CompilationProvenanceBuilder::module_id_at(std::size_t index) const noexcept
    -> ProgramModuleID {
    return storage.module_id_at(index);
}

auto CompilationProvenanceBuilder::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    return storage.spelling_id_at(index);
}

auto CompilationProvenanceBuilder::origin_id_at(std::size_t index) const noexcept
    -> ProgramOriginID {
    return storage.origin_id_at(index);
}

auto CompilationProvenanceBuilder::module_count() const noexcept -> std::size_t {
    return storage.modules.size();
}

auto CompilationProvenanceBuilder::reader() const noexcept -> CompilationProvenanceReader {
    return CompilationProvenanceReader(storage);
}

auto CompilationProvenanceBuilder::finish() && noexcept -> CompilationProvenance {
    auto provenance = CompilationProvenance(std::move(storage));
    const auto verification = verify_compilation_provenance(provenance.view());
    if (!verification.has_value()) {
        invariant_violation(verification.error().message);
    }
    return provenance;
}

auto CompilationProvenanceStorage::source_origin(ProgramOriginID id) const noexcept
    -> ProgramSourceOrigin {
    for (;;) {
        if (!contains(id)) {
            invariant_violation("program origin lookup used a foreign or invalid identity");
        }
        const auto& origin = origins[id.index()].value;
        if (const auto* source = std::get_if<ProgramSourceOrigin>(&origin)) {
            return *source;
        }
        id = std::get<ProgramExpansionOrigin>(origin).parent_origin_id;
    }
}
