module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.provenance;

import :source.manager;
import :source.module_path;
import :source.provenance;
import :source.provenance.verify;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace

static_assert(!std::copy_constructible<CompilationProvenance>);
static_assert(std::movable<CompilationProvenance>);

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
    CHECK_EQ(builder.source_snapshot(program_source_id).manager_source_id(), *source_id);
    CHECK_EQ(builder.find_program_module(path("sample")), module_id);
    CHECK_EQ(builder.module_record(module_id).source_id, program_source_id);
    CHECK_EQ(builder.module_count(), 1u);

    const auto spelling_id = builder.intern_spelling("beta");
    CHECK_EQ(builder.intern_spelling("beta"), spelling_id);
    const auto root_origin_id = builder.append_origin({
        .source_id = program_source_id,
        .span = Span::from_bounds(6, 10),
        .parent_origin_id = std::nullopt,
    });
    const auto derived_origin_id = builder.append_origin({
        .source_id = program_source_id,
        .span = Span::from_bounds(6, 10),
        .parent_origin_id = root_origin_id,
    });

    auto provenance = std::move(builder).finish();
    const auto view = provenance.view();
    REQUIRE(verify_compilation_provenance(view).has_value());

    REQUIRE_EQ(view.source_snapshots().size(), 1u);
    REQUIRE_EQ(view.module_records().size(), 1u);
    REQUIRE_EQ(view.spellings().size(), 1u);
    REQUIRE_EQ(view.origins().size(), 2u);
    CHECK_EQ(view.find_source_snapshot(*source_id), program_source_id);
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
    CHECK_EQ(view.origin(derived_origin_id).parent_origin_id, root_origin_id);

    auto resumed = CompilationProvenanceAppender(std::move(provenance));
    CHECK_EQ(resumed.view().find_program_module(path("sample")), module_id);
    CHECK_EQ(resumed.intern_spelling("beta"), spelling_id);
    const auto final_origin_id = resumed.append_origin({
        .source_id = program_source_id,
        .span = Span::from_bounds(6, 10),
        .parent_origin_id = derived_origin_id,
    });
    const auto extended = std::move(resumed).finish();
    const auto extended_view = extended.view();
    REQUIRE(verify_compilation_provenance(extended_view).has_value());
    CHECK_EQ(extended_view.origin(final_origin_id).parent_origin_id, derived_origin_id);
}
