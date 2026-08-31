module carven:source.provenance.impl;

import :source.provenance;
import :source.provenance.ids;
import :source.provenance.verify;
import :support.invariant;
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

CompilationProvenance::CompilationProvenance(CompilationProvenanceStorage storage_value) noexcept
    : storage(std::move(storage_value)) {}

auto CompilationProvenance::view() const noexcept -> CompilationProvenanceView {
    return CompilationProvenanceView(storage);
}

CompilationProvenanceView::CompilationProvenanceView(
    const CompilationProvenanceStorage& storage_value
) noexcept
    : provenance_storage(std::addressof(storage_value)) {}

auto CompilationProvenanceView::source_snapshot(ProgramSourceID source_id) const noexcept
    -> const ProgramSourceSnapshot& {
    return provenance_storage->sources.get(source_id);
}

auto CompilationProvenanceView::find_source_snapshot(SourceID source_id) const noexcept
    -> std::optional<ProgramSourceID> {
    const auto sources = provenance_storage->sources.values();
    const auto found = std::ranges::find_if(sources, [&](const auto& source) noexcept {
        return source.manager_source_id() == source_id;
    });
    if (found == sources.end()) {
        return std::nullopt;
    }
    return ProgramSourceID::from_index(static_cast<std::uint32_t>(found - sources.begin()));
}

auto CompilationProvenanceView::module_record(ProgramModuleID module_id) const noexcept
    -> const ProgramModule& {
    return provenance_storage->modules.get(module_id);
}

auto CompilationProvenanceView::find_program_module(const CanonicalModulePath& path) const noexcept
    -> std::optional<ProgramModuleID> {
    const auto modules = provenance_storage->modules.values();
    const auto found = std::ranges::find(modules, path, &ProgramModule::path);
    if (found == modules.end()) {
        return std::nullopt;
    }
    return ProgramModuleID::from_index(static_cast<std::uint32_t>(found - modules.begin()));
}

auto CompilationProvenanceView::spelling(ProgramSpellingID spelling_id) const noexcept
    -> std::string_view {
    return provenance_storage->spellings.get(spelling_id);
}

auto CompilationProvenanceView::origin(ProgramOriginID origin_id) const noexcept
    -> const ProgramOrigin& {
    return provenance_storage->origins.get(origin_id);
}

auto CompilationProvenanceView::source_snapshots() const noexcept
    -> std::span<const ProgramSourceSnapshot> {
    return provenance_storage->sources.values();
}

auto CompilationProvenanceView::module_records() const noexcept -> std::span<const ProgramModule> {
    return provenance_storage->modules.values();
}

auto CompilationProvenanceView::spellings() const noexcept -> std::span<const std::string> {
    return provenance_storage->spellings.values();
}

auto CompilationProvenanceView::origins() const noexcept -> std::span<const ProgramOrigin> {
    return provenance_storage->origins.values();
}

auto CompilationProvenanceView::source_span(ProgramOriginID origin_id) const noexcept
    -> SourceSpan {
    const auto& value = origin(origin_id);
    return {
        .source_id = source_snapshot(value.source_id).manager_source_id(),
        .span = value.span,
    };
}

auto CompilationProvenanceView::slice(ProgramOriginID origin_id) const noexcept
    -> std::string_view {
    const auto& value = origin(origin_id);
    return source_snapshot(value.source_id).slice(value.span);
}

auto CompilationProvenanceView::location(ProgramOriginID origin_id) const noexcept
    -> SourceLocation {
    const auto& value = origin(origin_id);
    return source_snapshot(value.source_id).location(value.span);
}

CompilationProvenanceAppender::CompilationProvenanceAppender(
    CompilationProvenance&& provenance
) noexcept
    : storage(std::move(provenance.storage)) {
    for (const auto& [index, spelling] : std::views::enumerate(storage.spellings.values())) {
        const auto spelling_id = ProgramSpellingID::from_index(static_cast<std::uint32_t>(index));
        spelling_ids_by_value.emplace(spelling, spelling_id);
    }
}

auto CompilationProvenanceAppender::intern_spelling(std::string_view spelling) noexcept
    -> ProgramSpellingID {
    if (const auto found = spelling_ids_by_value.find(spelling);
        found != spelling_ids_by_value.end()) {
        return found->second;
    }
    const auto spelling_id = storage.spellings.add(std::string(spelling));
    spelling_ids_by_value.emplace(storage.spellings.get(spelling_id), spelling_id);
    return spelling_id;
}

auto CompilationProvenanceAppender::append_origin(ProgramOrigin origin) noexcept
    -> ProgramOriginID {
    return storage.origins.add(std::move(origin));
}

auto CompilationProvenanceAppender::module_count() const noexcept -> std::size_t {
    return storage.modules.size();
}

auto CompilationProvenanceAppender::view() const noexcept -> CompilationProvenanceView {
    return CompilationProvenanceView(storage);
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
    if (program_module.source_id.index() >= storage.sources.size()) {
        invariant_violation("program module references an unknown source snapshot");
    }
    if (module_ids_by_path.contains(program_module.path)) {
        invariant_violation("program module path was added more than once");
    }
    if (std::ranges::contains(
            storage.modules.values(),
            program_module.source_id,
            &ProgramModule::source_id
        )) {
        invariant_violation("program source snapshot was assigned to more than one module");
    }

    const auto module_id = storage.modules.add(std::move(program_module));
    module_ids_by_path.emplace(storage.modules.get(module_id).path, module_id);
    return module_id;
}

auto CompilationProvenanceBuilder::intern_source_snapshot(SourceView source) noexcept
    -> ProgramSourceID {
    if (source.text.size() > std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("program source snapshot exceeds the 4 GiB span limit");
    }
    if (const auto found = program_source_ids_by_source_id.find(source.source_id);
        found != program_source_ids_by_source_id.end()) {
        const auto& existing = storage.sources.get(found->second);
        if (existing.display_origin() != source.origin
            || existing.size() != source.text.size()
            || existing.slice(Span::from_bounds(0, static_cast<std::uint32_t>(source.text.size())))
                != source.text) {
            invariant_violation("program source identity was rebound to another snapshot");
        }
        return found->second;
    }

    const auto source_id = storage.sources.add(ProgramSourceSnapshot(source));
    program_source_ids_by_source_id.emplace(source.source_id, source_id);
    return source_id;
}

auto CompilationProvenanceBuilder::intern_spelling(std::string_view spelling) noexcept
    -> ProgramSpellingID {
    if (const auto found = spelling_ids_by_value.find(spelling);
        found != spelling_ids_by_value.end()) {
        return found->second;
    }
    const auto spelling_id = storage.spellings.add(std::string(spelling));
    spelling_ids_by_value.emplace(storage.spellings.get(spelling_id), spelling_id);
    return spelling_id;
}

auto CompilationProvenanceBuilder::append_origin(ProgramOrigin origin) noexcept -> ProgramOriginID {
    return storage.origins.add(std::move(origin));
}

auto CompilationProvenanceBuilder::source_snapshot(ProgramSourceID source_id) const noexcept
    -> const ProgramSourceSnapshot& {
    return storage.sources.get(source_id);
}

auto CompilationProvenanceBuilder::find_source_snapshot(SourceID source_id) const noexcept
    -> std::optional<ProgramSourceID> {
    const auto found = program_source_ids_by_source_id.find(source_id);
    if (found == program_source_ids_by_source_id.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto CompilationProvenanceBuilder::module_record(ProgramModuleID module_id) const noexcept
    -> const ProgramModule& {
    return storage.modules.get(module_id);
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

auto CompilationProvenanceBuilder::spelling(ProgramSpellingID spelling_id) const noexcept
    -> std::string_view {
    return storage.spellings.get(spelling_id);
}

auto CompilationProvenanceBuilder::origin(ProgramOriginID origin_id) const noexcept
    -> const ProgramOrigin& {
    return storage.origins.get(origin_id);
}

auto CompilationProvenanceBuilder::module_records() const noexcept
    -> std::span<const ProgramModule> {
    return storage.modules.values();
}

auto CompilationProvenanceBuilder::origins() const noexcept -> std::span<const ProgramOrigin> {
    return storage.origins.values();
}

auto CompilationProvenanceBuilder::module_count() const noexcept -> std::size_t {
    return storage.modules.size();
}

auto CompilationProvenanceBuilder::source_span(ProgramOriginID origin_id) const noexcept
    -> SourceSpan {
    const auto& origin = storage.origins.get(origin_id);
    return {
        .source_id = storage.sources.get(origin.source_id).manager_source_id(),
        .span = origin.span,
    };
}

auto CompilationProvenanceBuilder::view() const noexcept -> CompilationProvenanceView {
    return CompilationProvenanceView(storage);
}

auto CompilationProvenanceBuilder::finish() && noexcept -> CompilationProvenance {
    auto provenance = CompilationProvenance(std::move(storage));
    const auto verification = verify_compilation_provenance(provenance.view());
    if (!verification.has_value()) {
        invariant_violation(verification.error().message);
    }
    return provenance;
}
