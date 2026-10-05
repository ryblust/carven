module carven:test.internal.source.provenance;

import :source.manager;
import :source.module_path;
import :source.provenance.verify;
import :source.provenance;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    require(result.has_value());
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

namespace {

const TestSuite suite([] static noexcept {
    "Program provenance: a frozen owner preserves source correlation"_test = [] static noexcept {
        auto sources = SourceManager();
        const auto source_id = sources.append_virtual("sample.cv", "alpha\nbeta\n");
        if (!expect(source_id.has_value())) {
            return;
        }

        auto builder = CompilationProvenanceBuilder();
        const auto program_source_id = builder.intern_source_snapshot(sources.view(*source_id));
        expect((builder.intern_source_snapshot(sources.view(*source_id)) == program_source_id));
        const auto module_id = builder.append_module({
            .source_id = program_source_id,
            .path = path("sample"),
        });
        expect((builder.find_source_snapshot(*source_id) == program_source_id));
        expect((builder.reader().source_manager_id(program_source_id) == *source_id));
        expect((builder.find_program_module(path("sample")) == module_id));
        expect((builder.reader().module_source(module_id) == program_source_id));
        expect_equal(builder.module_count(), 1u);

        const auto spelling_id = builder.intern_spelling("beta");
        expect((builder.intern_spelling("beta") == spelling_id));
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
        if (!expect(verify_compilation_provenance(view).has_value())) {
            return;
        }

        if (!expect_equal(view.source_snapshots().size(), 1u)) {
            return;
        }
        if (!expect_equal(view.module_records().size(), 1u)) {
            return;
        }
        if (!expect_equal(view.spellings().size(), 1u)) {
            return;
        }
        if (!expect_equal(view.origins().size(), 2u)) {
            return;
        }
        expect((view.find_program_module(path("sample")) == module_id));
        expect((view.module_record(module_id).source_id == program_source_id));
        expect((view.source_snapshot(program_source_id).manager_source_id() == *source_id));
        expect_equal(
            view.source_snapshot(program_source_id).display_origin(),
            std::string_view("sample.cv")
        );
        expect_equal(view.spelling(spelling_id), std::string_view("beta"));
        expect_equal(view.slice(root_origin_id), std::string_view("beta"));
        expect((view.source_span(root_origin_id).source_id == *source_id));
        expect((view.source_span(root_origin_id).span == Span::from_bounds(6, 10)));
        expect_equal(view.location(root_origin_id).line, 2u);
        expect_equal(view.location(root_origin_id).column, 1u);
        const auto& derived_origin =
            std::get<ProgramExpansionOrigin>(view.origin(derived_origin_id).value);
        expect((derived_origin.parent_origin_id == root_origin_id));
        expect_equal(derived_origin.reason, ProgramExpansionReason::ImplicitConversion);

        auto resumed = CompilationProvenanceAppender(std::move(provenance));
        expect((resumed.reader().find_program_module(path("sample")) == module_id));
        expect((resumed.intern_spelling("beta") == spelling_id));
        const auto construction_reader = resumed.reader();
        for (auto index = 0u; index < 256u; ++index) {
            static_cast<void>(resumed.intern_spelling(std::format("growth-{}", index)));
        }
        expect_equal(construction_reader.spelling_copy(spelling_id), std::string_view("beta"));
        const auto final_origin_id = resumed.append_origin({
            .value = ProgramExpansionOrigin {
                .parent_origin_id = derived_origin_id,
                .reason = ProgramExpansionReason::FailureTransport,
            },
        });
        const auto extended = std::move(resumed).finish();
        const auto extended_view = extended.view();
        if (!expect(verify_compilation_provenance(extended_view).has_value())) {
            return;
        }
        expect(
            (std::get<ProgramExpansionOrigin>(extended_view.origin(final_origin_id).value)
                 .parent_origin_id
             == derived_origin_id)
        );

        auto foreign_builder = CompilationProvenanceBuilder();
        const auto foreign_source_id =
            foreign_builder.intern_source_snapshot(sources.view(*source_id));
        expect(!(extended_view.contains(foreign_source_id)));
        expect((foreign_source_id.owner() != program_source_id.owner()));
    };

    "Program provenance: ownership transfers preserve one ID domain"_test = [] static noexcept {
        auto sources = SourceManager();
        const auto source_id = sources.append_virtual("move.cv", "value");
        if (!expect(source_id.has_value())) {
            return;
        }
        auto builder = CompilationProvenanceBuilder();
        const auto source = builder.intern_source_snapshot(sources.view(*source_id));
        const auto identity = builder.identity();
        auto moved = CompilationProvenanceBuilder(std::move(builder));
        auto program = std::move(moved).finish();
        auto appender = CompilationProvenanceAppender(std::move(program));
        const auto appended = std::move(appender).finish();
        expect((appended.view().identity() == identity));
        expect((appended.view().source_id_at(0uz) == source));
        expect_equal(appended.view().source_snapshot(source).text(), std::string_view("value"));
    };
});

} // namespace
