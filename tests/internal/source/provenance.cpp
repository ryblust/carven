module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.provenance;

import :source.manager;
import :source.module_path;
import :source.provenance.verify;
import :source.provenance;
import :test.internal.harness.death;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

template<typename Owner>
concept CanFinishLvalue = requires (Owner& owner) { owner.finish(); };

} // namespace

static_assert(!std::copy_constructible<CompilationProvenance>);
static_assert(std::move_constructible<CompilationProvenance>);
static_assert(std::move_constructible<CompilationProvenanceBuilder>);
static_assert(std::move_constructible<CompilationProvenanceAppender>);
static_assert(!CanFinishLvalue<CompilationProvenanceBuilder>);
static_assert(!CanFinishLvalue<CompilationProvenanceAppender>);
static_assert(std::same_as<
              decltype(std::declval<const CompilationProvenanceReader&>()
                           .spelling_copy(std::declval<ProgramSpellingID>())),
              std::string>);
static_assert(std::same_as<
              decltype(std::declval<const CompilationProvenanceReader&>()
                           .origin_copy(std::declval<ProgramOriginID>())),
              ProgramOrigin>);

TEST_CASE("Program provenance: a frozen owner preserves source correlation") {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("sample.cv", "alpha\nbeta\n");
    REQUIRE(source_id.has_value());

    auto builder = CompilationProvenanceBuilder();
    const auto program_source_id = builder.intern_source_snapshot(sources.view(*source_id));
    CHECK_EQ(builder.intern_source_snapshot(sources.view(*source_id)), program_source_id);
    const auto module_id = builder.append_module({
        .source_id = program_source_id,
        .path = path("sample"),
    });
    CHECK_EQ(builder.find_source_snapshot(*source_id), program_source_id);
    CHECK_EQ(builder.reader().source_manager_id(program_source_id), *source_id);
    CHECK_EQ(builder.find_program_module(path("sample")), module_id);
    CHECK_EQ(builder.reader().module_source(module_id), program_source_id);
    CHECK_EQ(builder.module_count(), 1u);

    const auto spelling_id = builder.intern_spelling("beta");
    CHECK_EQ(builder.intern_spelling("beta"), spelling_id);
    const auto root_origin_id = builder.append_origin({
        .value = ProgramSourceOrigin {
            .source_id = program_source_id,
            .span = Span::from_bounds(6, 10),
        },
    });
    const auto derived_origin_id = builder.append_origin({
        .value = ProgramExpansionOrigin {
            .parent_origin_id = root_origin_id,
            .reason = ProgramExpansionReason::ImplicitConversion,
        },
    });

    auto provenance = std::move(builder).finish();
    const auto view = provenance.view();
    REQUIRE(verify_compilation_provenance(view).has_value());

    REQUIRE_EQ(view.source_snapshots().size(), 1u);
    REQUIRE_EQ(view.module_records().size(), 1u);
    REQUIRE_EQ(view.spellings().size(), 1u);
    REQUIRE_EQ(view.origins().size(), 2u);
    CHECK_EQ(view.source_snapshot(program_source_id).manager_source_id(), *source_id);
    CHECK_EQ(view.find_program_module(path("sample")), module_id);
    CHECK_EQ(view.module_record(module_id).source_id, program_source_id);
    CHECK_EQ(view.source_snapshot(program_source_id).manager_source_id(), *source_id);
    CHECK_EQ(view.source_snapshot(program_source_id).display_origin(), "sample.cv");
    CHECK_EQ(view.spelling(spelling_id), "beta");
    CHECK_EQ(view.slice(root_origin_id), "beta");
    CHECK_EQ(view.source_span(root_origin_id).source_id, *source_id);
    CHECK_EQ(view.source_span(root_origin_id).span, Span::from_bounds(6, 10));
    CHECK_EQ(view.location(root_origin_id).line, 2u);
    CHECK_EQ(view.location(root_origin_id).column, 1u);
    const auto& derived_origin =
        std::get<ProgramExpansionOrigin>(view.origin(derived_origin_id).value);
    CHECK_EQ(derived_origin.parent_origin_id, root_origin_id);
    CHECK_EQ(derived_origin.reason, ProgramExpansionReason::ImplicitConversion);

    auto resumed = CompilationProvenanceAppender(std::move(provenance));
    CHECK_EQ(resumed.reader().find_program_module(path("sample")), module_id);
    CHECK_EQ(resumed.intern_spelling("beta"), spelling_id);
    const auto construction_reader = resumed.reader();
    for (auto index = 0u; index < 256u; ++index) {
        static_cast<void>(resumed.intern_spelling(std::format("growth-{}", index)));
    }
    CHECK_EQ(construction_reader.spelling_copy(spelling_id), "beta");
    const auto final_origin_id = resumed.append_origin({
        .value = ProgramExpansionOrigin {
            .parent_origin_id = derived_origin_id,
            .reason = ProgramExpansionReason::FailureTransport,
        },
    });
    const auto extended = std::move(resumed).finish();
    const auto extended_view = extended.view();
    REQUIRE(verify_compilation_provenance(extended_view).has_value());
    CHECK_EQ(
        std::get<ProgramExpansionOrigin>(extended_view.origin(final_origin_id).value)
            .parent_origin_id,
        derived_origin_id
    );

    auto foreign_builder = CompilationProvenanceBuilder();
    const auto foreign_source_id = foreign_builder.intern_source_snapshot(sources.view(*source_id));
    CHECK_FALSE(extended_view.contains(foreign_source_id));
    CHECK_NE(foreign_source_id.owner(), program_source_id.owner());
}

TEST_CASE("Program provenance: ownership transfers preserve one ID domain") {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("move.cv", "value");
    REQUIRE(source_id.has_value());
    auto builder = CompilationProvenanceBuilder();
    const auto source = builder.intern_source_snapshot(sources.view(*source_id));
    const auto identity = builder.identity();
    auto moved = CompilationProvenanceBuilder(std::move(builder));
    auto program = std::move(moved).finish();
    auto appender = CompilationProvenanceAppender(std::move(program));
    const auto appended = std::move(appender).finish();
    CHECK_EQ(appended.view().identity(), identity);
    CHECK_EQ(appended.view().source_id_at(0uz), source);
    CHECK_EQ(appended.view().source_snapshot(source).text(), "value");
}
