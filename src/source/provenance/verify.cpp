module carven:source.provenance.verify.impl;

import :source.provenance;
import :source.provenance.ids;
import :source.provenance.verify;
import std;

namespace {

auto verification_error(CompilationProvenanceErrorKind kind, std::string message) noexcept
    -> std::unexpected<CompilationProvenanceError> {
    return std::unexpected(
        CompilationProvenanceError {
            .kind = kind,
            .message = std::move(message),
        }
    );
}

} // namespace

auto verify_compilation_provenance(CompilationProvenanceView provenance) noexcept
    -> std::expected<void, CompilationProvenanceError> {
    auto source_ids = std::flat_set<SourceID>();
    for (const auto& source : provenance.source_snapshots()) {
        if (!source_ids.insert(source.manager_source_id()).second) {
            return verification_error(
                CompilationProvenanceErrorKind::DuplicateSource,
                std::format(
                    "program provenance contains source snapshot {} more than once",
                    source.manager_source_id().index()
                )
            );
        }
    }

    auto module_sources = std::flat_set<ProgramSourceID>();
    auto module_paths = std::flat_set<CanonicalModulePath>();
    for (auto index = 0uz; index < provenance.module_records().size(); ++index) {
        const auto& module_record = provenance.module_records()[index];
        if (module_record.source_id.index() >= provenance.source_snapshots().size()) {
            return verification_error(
                CompilationProvenanceErrorKind::MissingModuleSource,
                std::format("program module {} references an unknown source snapshot", index)
            );
        }
        if (!module_sources.insert(module_record.source_id).second) {
            return verification_error(
                CompilationProvenanceErrorKind::DuplicateModuleSource,
                std::format(
                    "program source snapshot @{} belongs to more than one module",
                    module_record.source_id.index()
                )
            );
        }
        if (!module_paths.insert(module_record.path).second) {
            return verification_error(
                CompilationProvenanceErrorKind::DuplicateModulePath,
                std::format(
                    "program module path '{}' occurs more than once",
                    module_record.path.value()
                )
            );
        }
        if (index != 0 && module_record.path < provenance.module_records()[index - 1].path) {
            return verification_error(
                CompilationProvenanceErrorKind::UnorderedModules,
                "program modules are not in canonical path order"
            );
        }
    }

    auto spellings = std::flat_set<std::string_view, std::less<>>();
    for (const auto& spelling : provenance.spellings()) {
        if (!spellings.insert(spelling).second) {
            return verification_error(
                CompilationProvenanceErrorKind::DuplicateSpelling,
                "program provenance contains a duplicate spelling"
            );
        }
    }

    const auto origins = provenance.origins();
    for (auto index = 0uz; index < origins.size(); ++index) {
        const auto origin_id = ProgramOriginID::from_index(static_cast<std::uint32_t>(index));
        const auto& origin = origins[index];
        if (origin.source_id.index() >= provenance.source_snapshots().size()) {
            return verification_error(
                CompilationProvenanceErrorKind::MissingOriginSource,
                std::format("program origin {} references an unknown source snapshot", index)
            );
        }
        if (origin.span.end() > provenance.source_snapshot(origin.source_id).size()) {
            return verification_error(
                CompilationProvenanceErrorKind::InvalidOriginSpan,
                std::format("program origin {} extends beyond its source snapshot", index)
            );
        }
        if (origin.parent_origin_id.has_value()
            && origin.parent_origin_id->index() >= origin_id.index()) {
            return verification_error(
                CompilationProvenanceErrorKind::InvalidParentOrigin,
                std::format("program origin {} does not reference an earlier parent origin", index)
            );
        }
    }

    return {};
}
