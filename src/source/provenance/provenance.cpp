module carven:source.provenance.impl;

import :source.provenance;
import :source.provenance.ids;
import :source.provenance.verify;
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
    : active(true),
      generation(1u),
      provenance_identity(ProvenanceIdentity::fresh()) {}

CompilationProvenanceStorage::CompilationProvenanceStorage(
    CompilationProvenanceStorage&& other
) noexcept
    : active(std::exchange(other.active, false)),
      generation(other.generation),
      provenance_identity(other.provenance_identity),
      sources(std::move(other.sources)),
      modules(std::move(other.modules)),
      spellings(std::move(other.spellings)),
      origins(std::move(other.origins)) {
    if (!active) {
        invariant_violation("inactive compilation provenance storage was moved");
    }
}

auto CompilationProvenanceStorage::operator=(CompilationProvenanceStorage&& other) noexcept
    -> CompilationProvenanceStorage& {
    if (this == std::addressof(other)) {
        invariant_violation("compilation provenance storage was moved into itself");
    }
    other.require_active();
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        resource_limit_exceeded("compilation provenance storage generation exhausted");
    }
    active = true;
    ++generation;
    provenance_identity = other.provenance_identity;
    sources = std::move(other.sources);
    modules = std::move(other.modules);
    spellings = std::move(other.spellings);
    origins = std::move(other.origins);
    other.active = false;
    return *this;
}

auto CompilationProvenanceStorage::require_active() const noexcept -> void {
    if (!active) {
        invariant_violation("compilation provenance storage was used after consumption");
    }
}

auto CompilationProvenanceStorage::require_generation(std::uint64_t expected) const noexcept
    -> void {
    require_active();
    if (generation != expected) {
        invariant_violation("compilation provenance view outlived an owner replacement");
    }
}

auto CompilationProvenanceStorage::source_id_at(std::size_t index) const noexcept
    -> ProgramSourceID {
    require_active();
    if (index >= sources.size()) {
        invariant_violation("program source index is out of bounds");
    }
    return ProgramSourceID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::module_id_at(std::size_t index) const noexcept
    -> ProgramModuleID {
    require_active();
    if (index >= modules.size()) {
        invariant_violation("program module index is out of bounds");
    }
    return ProgramModuleID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    require_active();
    if (index >= spellings.size()) {
        invariant_violation("program spelling index is out of bounds");
    }
    return ProgramSpellingID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::origin_id_at(std::size_t index) const noexcept
    -> ProgramOriginID {
    require_active();
    if (index >= origins.size()) {
        invariant_violation("program origin index is out of bounds");
    }
    return ProgramOriginID(provenance_identity, static_cast<std::uint32_t>(index));
}

auto CompilationProvenanceStorage::append_source(ProgramSourceSnapshot source) noexcept
    -> ProgramSourceID {
    require_active();
    if (sources.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program source identity space exhausted");
    }
    sources.push_back(std::move(source));
    return source_id_at(sources.size() - 1uz);
}

auto CompilationProvenanceStorage::append_module(ProgramModule module_record) noexcept
    -> ProgramModuleID {
    require_active();
    if (modules.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program module identity space exhausted");
    }
    modules.push_back(std::move(module_record));
    return module_id_at(modules.size() - 1uz);
}

auto CompilationProvenanceStorage::append_spelling(std::string spelling) noexcept
    -> ProgramSpellingID {
    require_active();
    if (spellings.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program spelling identity space exhausted");
    }
    spellings.push_back(std::move(spelling));
    return spelling_id_at(spellings.size() - 1uz);
}

auto CompilationProvenanceStorage::append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID {
    require_active();
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
    require_active();
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < sources.size();
}

auto CompilationProvenanceStorage::contains(ProgramModuleID id) const noexcept -> bool {
    require_active();
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < modules.size();
}

auto CompilationProvenanceStorage::contains(ProgramSpellingID id) const noexcept -> bool {
    require_active();
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < spellings.size();
}

auto CompilationProvenanceStorage::contains(ProgramOriginID id) const noexcept -> bool {
    require_active();
    return id.owner() == provenance_identity
        && static_cast<std::size_t>(id.index()) < origins.size();
}

CompilationProvenance::CompilationProvenance(CompilationProvenanceStorage storage_value) noexcept
    : storage(std::move(storage_value)) {}

CompilationProvenance::CompilationProvenance(CompilationProvenance&& other) noexcept
    : storage(std::move(other.storage)) {}

auto CompilationProvenance::operator=(CompilationProvenance&& other) noexcept
    -> CompilationProvenance& {
    storage = std::move(other.storage);
    return *this;
}

auto CompilationProvenance::view() const noexcept -> CompilationProvenanceView {
    storage.require_active();
    return CompilationProvenanceView(storage);
}

CompilationProvenanceView::CompilationProvenanceView(
    const CompilationProvenanceStorage& storage_value
) noexcept
    : provenance_storage(std::addressof(storage_value)),
      storage_generation(storage_value.generation) {
    storage_value.require_active();
}

auto CompilationProvenanceView::require_active() const noexcept -> void {
    provenance_storage->require_generation(storage_generation);
}

auto CompilationProvenanceView::identity() const noexcept -> ProvenanceIdentity {
    require_active();
    return provenance_storage->provenance_identity;
}

auto CompilationProvenanceView::contains(ProgramSourceID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceView::contains(ProgramModuleID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceView::contains(ProgramSpellingID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceView::contains(ProgramOriginID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceView::source_id_at(std::size_t index) const noexcept -> ProgramSourceID {
    require_active();
    return provenance_storage->source_id_at(index);
}

auto CompilationProvenanceView::module_id_at(std::size_t index) const noexcept -> ProgramModuleID {
    require_active();
    return provenance_storage->module_id_at(index);
}

auto CompilationProvenanceView::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    require_active();
    return provenance_storage->spelling_id_at(index);
}

auto CompilationProvenanceView::origin_id_at(std::size_t index) const noexcept -> ProgramOriginID {
    require_active();
    return provenance_storage->origin_id_at(index);
}

auto CompilationProvenanceView::source_snapshot(ProgramSourceID source_id) const noexcept
    -> const ProgramSourceSnapshot& {
    require_active();
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage->sources[source_id.index()];
}

auto CompilationProvenanceView::find_source_snapshot(SourceID source_id) const noexcept
    -> std::optional<ProgramSourceID> {
    require_active();
    const auto& sources = provenance_storage->sources;
    const auto found = std::ranges::find_if(sources, [&](const auto& source) noexcept {
        return source.manager_source_id() == source_id;
    });
    if (found == sources.end()) {
        return std::nullopt;
    }
    return source_id_at(static_cast<std::size_t>(found - sources.begin()));
}

auto CompilationProvenanceView::module_record(ProgramModuleID module_id) const noexcept
    -> const ProgramModule& {
    require_active();
    if (!contains(module_id)) {
        invariant_violation("program module lookup used a foreign or invalid identity");
    }
    return provenance_storage->modules[module_id.index()];
}

auto CompilationProvenanceView::find_program_module(const CanonicalModulePath& path) const noexcept
    -> std::optional<ProgramModuleID> {
    require_active();
    const auto& modules = provenance_storage->modules;
    const auto found = std::ranges::find(modules, path, &ProgramModule::path);
    if (found == modules.end()) {
        return std::nullopt;
    }
    return module_id_at(static_cast<std::size_t>(found - modules.begin()));
}

auto CompilationProvenanceView::spelling(ProgramSpellingID spelling_id) const noexcept
    -> std::string_view {
    require_active();
    if (!contains(spelling_id)) {
        invariant_violation("program spelling lookup used a foreign or invalid identity");
    }
    return provenance_storage->spellings[spelling_id.index()];
}

auto CompilationProvenanceView::origin(ProgramOriginID origin_id) const noexcept
    -> const ProgramOrigin& {
    require_active();
    if (!contains(origin_id)) {
        invariant_violation("program origin lookup used a foreign or invalid identity");
    }
    return provenance_storage->origins[origin_id.index()];
}

auto CompilationProvenanceView::source_snapshots() const noexcept
    -> std::span<const ProgramSourceSnapshot> {
    require_active();
    return provenance_storage->sources;
}

auto CompilationProvenanceView::module_records() const noexcept -> std::span<const ProgramModule> {
    require_active();
    return provenance_storage->modules;
}

auto CompilationProvenanceView::spellings() const noexcept -> std::span<const std::string> {
    require_active();
    return provenance_storage->spellings;
}

auto CompilationProvenanceView::origins() const noexcept -> std::span<const ProgramOrigin> {
    require_active();
    return provenance_storage->origins;
}

auto CompilationProvenanceView::source_span(ProgramOriginID origin_id) const noexcept
    -> SourceSpan {
    require_active();
    auto current = origin_id;
    const ProgramSourceOrigin* source_origin = nullptr;
    while (source_origin == nullptr) {
        const auto& value = origin(current).value;
        if (const auto* source = std::get_if<ProgramSourceOrigin>(&value)) {
            source_origin = source;
            continue;
        }
        current = std::get<ProgramExpansionOrigin>(value).parent_origin_id;
    }
    return {
        .source_id = source_snapshot(source_origin->source_id).manager_source_id(),
        .span = source_origin->span,
    };
}

auto CompilationProvenanceView::slice(ProgramOriginID origin_id) const noexcept
    -> std::string_view {
    require_active();
    const auto source = source_span(origin_id);
    const auto source_id = find_source_snapshot(source.source_id);
    if (!source_id.has_value()) {
        invariant_violation("program origin resolved outside its provenance source owner");
    }
    return source_snapshot(*source_id).slice(source.span);
}

auto CompilationProvenanceView::location(ProgramOriginID origin_id) const noexcept
    -> SourceLocation {
    require_active();
    const auto source = source_span(origin_id);
    const auto source_id = find_source_snapshot(source.source_id);
    if (!source_id.has_value()) {
        invariant_violation("program origin resolved outside its provenance source owner");
    }
    return source_snapshot(*source_id).location(source.span);
}

CompilationProvenanceReader::CompilationProvenanceReader(
    const CompilationProvenanceStorage& storage_value
) noexcept
    : provenance_storage(std::addressof(storage_value)),
      storage_generation(storage_value.generation) {
    storage_value.require_active();
}

auto CompilationProvenanceReader::require_active() const noexcept -> void {
    provenance_storage->require_generation(storage_generation);
}

auto CompilationProvenanceReader::identity() const noexcept -> ProvenanceIdentity {
    require_active();
    return provenance_storage->provenance_identity;
}

auto CompilationProvenanceReader::contains(ProgramSourceID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceReader::contains(ProgramModuleID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceReader::contains(ProgramSpellingID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceReader::contains(ProgramOriginID id) const noexcept -> bool {
    require_active();
    return provenance_storage->contains(id);
}

auto CompilationProvenanceReader::source_id_at(std::size_t index) const noexcept
    -> ProgramSourceID {
    require_active();
    return provenance_storage->source_id_at(index);
}

auto CompilationProvenanceReader::module_id_at(std::size_t index) const noexcept
    -> ProgramModuleID {
    require_active();
    return provenance_storage->module_id_at(index);
}

auto CompilationProvenanceReader::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    require_active();
    return provenance_storage->spelling_id_at(index);
}

auto CompilationProvenanceReader::origin_id_at(std::size_t index) const noexcept
    -> ProgramOriginID {
    require_active();
    return provenance_storage->origin_id_at(index);
}

auto CompilationProvenanceReader::source_count() const noexcept -> std::size_t {
    require_active();
    return provenance_storage->sources.size();
}

auto CompilationProvenanceReader::module_count() const noexcept -> std::size_t {
    require_active();
    return provenance_storage->modules.size();
}

auto CompilationProvenanceReader::spelling_count() const noexcept -> std::size_t {
    require_active();
    return provenance_storage->spellings.size();
}

auto CompilationProvenanceReader::origin_count() const noexcept -> std::size_t {
    require_active();
    return provenance_storage->origins.size();
}

auto CompilationProvenanceReader::source_manager_id(ProgramSourceID source_id) const noexcept
    -> SourceID {
    require_active();
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage->sources[source_id.index()].manager_source_id();
}

auto CompilationProvenanceReader::source_display_origin_copy(
    ProgramSourceID source_id
) const noexcept -> std::string {
    require_active();
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return std::string(provenance_storage->sources[source_id.index()].display_origin());
}

auto CompilationProvenanceReader::source_size(ProgramSourceID source_id) const noexcept
    -> std::size_t {
    require_active();
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage->sources[source_id.index()].size();
}

auto CompilationProvenanceReader::source_slice_copy(
    ProgramSourceID source_id,
    Span span
) const noexcept -> std::string {
    require_active();
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return std::string(provenance_storage->sources[source_id.index()].slice(span));
}

auto CompilationProvenanceReader::source_location(
    ProgramSourceID source_id,
    Span span
) const noexcept -> SourceLocation {
    require_active();
    if (!contains(source_id)) {
        invariant_violation("program source lookup used a foreign or invalid identity");
    }
    return provenance_storage->sources[source_id.index()].location(span);
}

auto CompilationProvenanceReader::find_source_snapshot(SourceID source_id) const noexcept
    -> std::optional<ProgramSourceID> {
    require_active();
    const auto& sources = provenance_storage->sources;
    const auto found = std::ranges::find_if(sources, [&](const auto& source) noexcept {
        return source.manager_source_id() == source_id;
    });
    if (found == sources.end()) {
        return std::nullopt;
    }
    return source_id_at(static_cast<std::size_t>(found - sources.begin()));
}

auto CompilationProvenanceReader::module_source(ProgramModuleID module_id) const noexcept
    -> ProgramSourceID {
    require_active();
    if (!contains(module_id)) {
        invariant_violation("program module lookup used a foreign or invalid identity");
    }
    return provenance_storage->modules[module_id.index()].source_id;
}

auto CompilationProvenanceReader::module_path_copy(ProgramModuleID module_id) const noexcept
    -> CanonicalModulePath {
    require_active();
    if (!contains(module_id)) {
        invariant_violation("program module lookup used a foreign or invalid identity");
    }
    return provenance_storage->modules[module_id.index()].path;
}

auto CompilationProvenanceReader::find_program_module(
    const CanonicalModulePath& path
) const noexcept -> std::optional<ProgramModuleID> {
    require_active();
    const auto& modules = provenance_storage->modules;
    const auto found = std::ranges::find(modules, path, &ProgramModule::path);
    if (found == modules.end()) {
        return std::nullopt;
    }
    return module_id_at(static_cast<std::size_t>(found - modules.begin()));
}

auto CompilationProvenanceReader::spelling_copy(ProgramSpellingID spelling_id) const noexcept
    -> std::string {
    require_active();
    if (!contains(spelling_id)) {
        invariant_violation("program spelling lookup used a foreign or invalid identity");
    }
    return provenance_storage->spellings[spelling_id.index()];
}

auto CompilationProvenanceReader::origin_copy(ProgramOriginID origin_id) const noexcept
    -> ProgramOrigin {
    require_active();
    if (!contains(origin_id)) {
        invariant_violation("program origin lookup used a foreign or invalid identity");
    }
    return provenance_storage->origins[origin_id.index()];
}

auto CompilationProvenanceReader::source_span(ProgramOriginID origin_id) const noexcept
    -> SourceSpan {
    require_active();
    auto current = origin_id;
    while (true) {
        const auto origin = origin_copy(current);
        if (const auto* source = std::get_if<ProgramSourceOrigin>(&origin.value)) {
            return {
                .source_id = source_manager_id(source->source_id),
                .span = source->span,
            };
        }
        current = std::get<ProgramExpansionOrigin>(origin.value).parent_origin_id;
    }
}

auto CompilationProvenanceReader::slice_copy(ProgramOriginID origin_id) const noexcept
    -> std::string {
    require_active();
    const auto source = source_span(origin_id);
    const auto source_id = find_source_snapshot(source.source_id);
    if (!source_id.has_value()) {
        invariant_violation("program origin resolved outside its provenance source owner");
    }
    return source_slice_copy(*source_id, source.span);
}

auto CompilationProvenanceReader::location(ProgramOriginID origin_id) const noexcept
    -> SourceLocation {
    require_active();
    const auto source = source_span(origin_id);
    const auto source_id = find_source_snapshot(source.source_id);
    if (!source_id.has_value()) {
        invariant_violation("program origin resolved outside its provenance source owner");
    }
    return source_location(*source_id, source.span);
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

CompilationProvenanceAppender::CompilationProvenanceAppender(
    CompilationProvenanceAppender&& other
) noexcept
    : storage(std::move(other.storage)),
      spelling_ids_by_value(std::move(other.spelling_ids_by_value)) {}

auto CompilationProvenanceAppender::operator=(CompilationProvenanceAppender&& other) noexcept
    -> CompilationProvenanceAppender& {
    storage = std::move(other.storage);
    spelling_ids_by_value = std::move(other.spelling_ids_by_value);
    return *this;
}

auto CompilationProvenanceAppender::intern_spelling(std::string_view spelling) noexcept
    -> ProgramSpellingID {
    storage.require_active();
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
    storage.require_active();
    return storage.append_origin(origin);
}

auto CompilationProvenanceAppender::module_count() const noexcept -> std::size_t {
    storage.require_active();
    return storage.modules.size();
}

auto CompilationProvenanceAppender::reader() const noexcept -> CompilationProvenanceReader {
    storage.require_active();
    return CompilationProvenanceReader(storage);
}

auto CompilationProvenanceAppender::finish() && noexcept -> CompilationProvenance {
    storage.require_active();
    auto provenance = CompilationProvenance(std::move(storage));
    const auto verification = verify_compilation_provenance(provenance.view());
    if (!verification.has_value()) {
        invariant_violation(verification.error().message);
    }
    return provenance;
}

CompilationProvenanceBuilder::CompilationProvenanceBuilder(
    CompilationProvenanceBuilder&& other
) noexcept
    : storage(std::move(other.storage)),
      program_source_ids_by_source_id(std::move(other.program_source_ids_by_source_id)),
      module_ids_by_path(std::move(other.module_ids_by_path)),
      spelling_ids_by_value(std::move(other.spelling_ids_by_value)) {}

auto CompilationProvenanceBuilder::operator=(CompilationProvenanceBuilder&& other) noexcept
    -> CompilationProvenanceBuilder& {
    storage = std::move(other.storage);
    program_source_ids_by_source_id = std::move(other.program_source_ids_by_source_id);
    module_ids_by_path = std::move(other.module_ids_by_path);
    spelling_ids_by_value = std::move(other.spelling_ids_by_value);
    return *this;
}

auto CompilationProvenanceBuilder::append_module(ProgramModule program_module) noexcept
    -> ProgramModuleID {
    storage.require_active();
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
    storage.require_active();
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
    storage.require_active();
    if (const auto found = spelling_ids_by_value.find(spelling);
        found != spelling_ids_by_value.end()) {
        return found->second;
    }
    const auto spelling_id = storage.append_spelling(std::string(spelling));
    spelling_ids_by_value.emplace(storage.spellings[spelling_id.index()], spelling_id);
    return spelling_id;
}

auto CompilationProvenanceBuilder::append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID {
    storage.require_active();
    return storage.append_origin(origin);
}

auto CompilationProvenanceBuilder::find_source_snapshot(SourceID source_id) const noexcept
    -> std::optional<ProgramSourceID> {
    storage.require_active();
    const auto found = program_source_ids_by_source_id.find(source_id);
    if (found == program_source_ids_by_source_id.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto CompilationProvenanceBuilder::find_program_module(
    const CanonicalModulePath& path
) const noexcept -> std::optional<ProgramModuleID> {
    storage.require_active();
    const auto found = module_ids_by_path.find(path);
    if (found == module_ids_by_path.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto CompilationProvenanceBuilder::identity() const noexcept -> ProvenanceIdentity {
    storage.require_active();
    return storage.provenance_identity;
}

auto CompilationProvenanceBuilder::source_id_at(std::size_t index) const noexcept
    -> ProgramSourceID {
    storage.require_active();
    return storage.source_id_at(index);
}

auto CompilationProvenanceBuilder::module_id_at(std::size_t index) const noexcept
    -> ProgramModuleID {
    storage.require_active();
    return storage.module_id_at(index);
}

auto CompilationProvenanceBuilder::spelling_id_at(std::size_t index) const noexcept
    -> ProgramSpellingID {
    storage.require_active();
    return storage.spelling_id_at(index);
}

auto CompilationProvenanceBuilder::origin_id_at(std::size_t index) const noexcept
    -> ProgramOriginID {
    storage.require_active();
    return storage.origin_id_at(index);
}

auto CompilationProvenanceBuilder::module_count() const noexcept -> std::size_t {
    storage.require_active();
    return storage.modules.size();
}

auto CompilationProvenanceBuilder::reader() const noexcept -> CompilationProvenanceReader {
    storage.require_active();
    return CompilationProvenanceReader(storage);
}

auto CompilationProvenanceBuilder::finish() && noexcept -> CompilationProvenance {
    storage.require_active();
    auto provenance = CompilationProvenance(std::move(storage));
    const auto verification = verify_compilation_provenance(provenance.view());
    if (!verification.has_value()) {
        invariant_violation(verification.error().message);
    }
    return provenance;
}
