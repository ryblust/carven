module carven:test.internal.semantic.analysis.coverage;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.coverage;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    ct::require(result.has_value());
    return std::move(*result);
}

struct CoverageFixture final {
    ProgramDraft compilation;
    BodyReservation reservation;
    ProgramOriginID origin;
    TypeID boolean;
    TypeID pair_enum;
    TypeID empty_enum;
    EnumCaseID pair_case;
    EnumCaseID empty_case;
};

auto fixture(SourceManager& sources, DiagnosticSink& diagnostics, std::size_t width = 2uz) noexcept
    -> CoverageFixture {
    const auto source = sources.append_virtual("coverage-fixture.cv", "");
    ct::require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = path("coverage.fixture"),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    ct::require(syntax.has_value());
    auto compilation = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = compilation.provenance_module_at(0uz);
    const auto source_id = compilation.module_source(provenance_module);
    const auto origin = compilation.append_source_origin(source_id, Span::at(0u));
    const auto boolean = compilation.builtin_type(BuiltinType::Bool);

    const auto module_id = compilation.reserve_module_declaration();
    const auto enumeration = compilation.reserve_enum_declaration();
    const auto dead_case = compilation.reserve_enum_case_declaration();
    const auto pair_case = compilation.reserve_enum_case_declaration();
    const auto empty_case = compilation.reserve_enum_case_declaration();
    const auto uninhabited = compilation.reserve_enum_declaration();
    const auto test = compilation.reserve_test();
    const auto pair_enum = compilation.intern_type(
        CanonicalType {
            .value = EnumTypeValue {.enumeration = enumeration},
        }
    );
    const auto empty_enum = compilation.intern_type(
        CanonicalType {
            .value = EnumTypeValue {.enumeration = uninhabited},
        }
    );

    compilation.define_declaration(
        dead_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = compilation.intern_spelling("Dead"),
            .origin = origin,
            .payload_types = {empty_enum},
            .constant = std::nullopt,
        }
    );
    compilation.define_declaration(
        pair_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = compilation.intern_spelling("Pair"),
            .origin = origin,
            .payload_types = std::vector<ConstructionTypeRef>(width, boolean),
            .constant = std::nullopt,
        }
    );
    compilation.define_declaration(
        empty_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = compilation.intern_spelling("Empty"),
            .origin = origin,
            .payload_types = {},
            .constant = std::nullopt,
        }
    );
    compilation.define_declaration(
        enumeration,
        EnumDeclaration {
            .module_id = module_id,
            .name = compilation.intern_spelling("PairError"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .representation = PayloadEnumRepresentation {},
            .cases = {dead_case, pair_case, empty_case},
            .supports_equality = true,
        }
    );
    compilation.define_declaration(
        uninhabited,
        EnumDeclaration {
            .module_id = module_id,
            .name = compilation.intern_spelling("Never"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .representation = PayloadEnumRepresentation {},
            .cases = {},
            .supports_equality = true,
        }
    );
    compilation.define_declaration(
        module_id,
        ModuleDeclaration {
            .provenance_module = provenance_module,
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {ModuleItem {enumeration}, ModuleItem {uninhabited}, ModuleItem {test}},
        }
    );
    compilation.finish_declaration_heads();

    auto reservation = compilation.reserve_body(BodyKind::Test);
    compilation.define_test(
        test,
        TestDeclaration {
            .is_const = false,
            .module_id = module_id,
            .source = {.label = compilation.intern_spelling("coverage"), .origin = origin},
            .body = reservation.id(),
        }
    );
    return CoverageFixture {
        .compilation = std::move(compilation),
        .reservation = std::move(reservation),
        .origin = origin,
        .boolean = boolean,
        .pair_enum = pair_enum,
        .empty_enum = empty_enum,
        .pair_case = pair_case,
        .empty_case = empty_case,
    };
}

auto wildcard(BodyBuilder& body, ConstructionTypeRef type, ProgramOriginID origin) noexcept
    -> PatternID {
    return body.add_pattern(
        ElaboratedPattern {
            .type = type,
            .value = WildcardPattern {},
            .origin = origin,
        }
    );
}

auto boolean_literal(CoverageFixture& source, BodyBuilder& body, bool value) noexcept -> PatternID {
    const auto constant = source.compilation.intern_constant(
        ConstantFact {
            .type = source.boolean,
            .value = BooleanConstant {.value = value},
        }
    );
    return body.add_pattern(
        ElaboratedPattern {
            .type = source.boolean,
            .value = LiteralPattern {.constant = constant},
            .origin = source.origin,
        }
    );
}

auto enum_case(
    CoverageFixture& source,
    BodyBuilder& body,
    EnumCaseID member,
    std::vector<PatternID> payload
) noexcept -> PatternID {
    return body.add_pattern(
        ElaboratedPattern {
            .type = source.pair_enum,
            .value =
                EnumCasePattern {
                    .enum_case = member,
                    .payload = std::move(payload),
                },
            .origin = source.origin,
        }
    );
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Pattern coverage: enum payload overlap and guarded exhaustiveness stay exact",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto source = fixture(sources, diagnostics);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto true_pattern = boolean_literal(source, body, true);
            const auto first_pair = enum_case(
                source,
                body,
                source.pair_case,
                {true_pattern, wildcard(body, source.boolean, source.origin)}
            );
            const auto second_pair = enum_case(
                source,
                body,
                source.pair_case,
                {wildcard(body, source.boolean, source.origin), true_pattern}
            );
            const auto every_pair = enum_case(
                source,
                body,
                source.pair_case,
                {
                    wildcard(body, source.boolean, source.origin),
                    wildcard(body, source.boolean, source.origin),
                }
            );
            const auto empty = enum_case(source, body, source.empty_case, {});
            const auto arms = std::array {
                PatternCoverageArm {
                    .alternatives = {first_pair, second_pair},
                    .guarded = false,
                },
                PatternCoverageArm {.alternatives = {every_pair}, .guarded = false},
                PatternCoverageArm {.alternatives = {empty}, .guarded = true},
                PatternCoverageArm {.alternatives = {empty}, .guarded = false},
                PatternCoverageArm {.alternatives = {std::nullopt}, .guarded = false},
            };
            auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(
                (coverage->arm_usefulness == std::vector<bool> {true, true, true, true, false})
            );
            ct::expect(
                (coverage->exhaustive_after_arm
                 == std::vector<bool> {false, false, false, true, true})
            );
            ct::expect(coverage->exhaustive);
            ct::expect(coverage->redundant_alternatives.empty());
            ct::expect(diagnostics.empty());
        }
    );

    ct::test(
        "Pattern coverage: redundant alternatives and finite witnesses are reported",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto source = fixture(sources, diagnostics);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto true_pattern = boolean_literal(source, body, true);
            const auto arms = std::array {
                PatternCoverageArm {
                    .alternatives = {true_pattern, true_pattern},
                    .guarded = true,
                },
            };
            auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.boolean,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(!(coverage->exhaustive));
            ct::expect_equal(coverage->missing_witness, std::string_view("false"));
            if (!ct::expect_equal(coverage->redundant_alternatives.size(), 1uz)) {
                return;
            }
            ct::expect_equal(coverage->redundant_alternatives.front().arm, 0uz);
            ct::expect_equal(coverage->redundant_alternatives.front().alternative, 1uz);
            ct::expect(diagnostics.empty());
        }
    );

    ct::test(
        "Pattern coverage: an alternative covered by a union is redundant",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto source = fixture(sources, diagnostics);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto true_pattern = boolean_literal(source, body, true);
            const auto false_pattern = boolean_literal(source, body, false);
            const auto row = enum_case(
                source,
                body,
                source.pair_case,
                {true_pattern, wildcard(body, source.boolean, source.origin)}
            );
            const auto true_column = enum_case(
                source,
                body,
                source.pair_case,
                {wildcard(body, source.boolean, source.origin), true_pattern}
            );
            const auto false_column = enum_case(
                source,
                body,
                source.pair_case,
                {wildcard(body, source.boolean, source.origin), false_pattern}
            );
            const auto arms = std::array {
                PatternCoverageArm {
                    .alternatives = {row, true_column, false_column},
                    .guarded = false,
                },
            };
            auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            if (!ct::expect_equal(coverage->redundant_alternatives.size(), 1uz)) {
                return;
            }
            ct::expect_equal(coverage->redundant_alternatives.front().arm, 0uz);
            ct::expect_equal(coverage->redundant_alternatives.front().alternative, 0uz);
            ct::expect(!(coverage->alternative_usefulness.front().front()));
            ct::expect(diagnostics.empty());
        }
    );

    ct::test(
        "Pattern coverage: witnesses retain missing nested payload constructors",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto source = fixture(sources, diagnostics);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto true_pattern = boolean_literal(source, body, true);
            const auto first_row = enum_case(
                source,
                body,
                source.pair_case,
                {true_pattern, wildcard(body, source.boolean, source.origin)}
            );
            const auto empty = enum_case(source, body, source.empty_case, {});
            const auto arms = std::array {
                PatternCoverageArm {.alternatives = {first_row}, .guarded = false},
                PatternCoverageArm {.alternatives = {empty}, .guarded = false},
            };
            auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(!(coverage->exhaustive));
            ct::expect_equal(coverage->missing_witness, std::string_view(".Pair(false, false)"));
            ct::expect(diagnostics.empty());
        }
    );

    ct::test("Pattern coverage: an enum with no constructors is exhaustive", [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto source = fixture(sources, diagnostics);
        const auto body = BodyBuilder(std::move(source.reservation), source.compilation);
        const auto arms = std::array<PatternCoverageArm, 0> {};
        auto coverage = compute_pattern_coverage(
            source.compilation,
            body.pattern_table(),
            source.empty_enum,
            arms
        );
        if (!ct::expect(coverage.has_value())) {
            return;
        }
        ct::expect(coverage->exhaustive);
        ct::expect(coverage->arm_usefulness.empty());
        ct::expect(diagnostics.empty());
    });

    ct::test(
        "Pattern coverage: wide payloads retain missing and covered value classes",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            constexpr auto width = 32uz;
            auto source = fixture(sources, diagnostics, width);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto selected = enum_case(
                source,
                body,
                source.pair_case,
                std::vector<PatternID>(width, boolean_literal(source, body, true))
            );
            auto arms = std::vector<PatternCoverageArm> {
                {.alternatives = {selected}, .guarded = false},
            };
            auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(!(coverage->exhaustive));
            ct::expect(coverage->arm_usefulness.front());
            arms.push_back({.alternatives = {std::nullopt}, .guarded = false});
            coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(coverage->exhaustive);
            ct::expect(coverage->arm_usefulness == std::vector<bool> {true, true});
        }
    );

    ct::test(
        "Pattern coverage: uninhabited cases do not hide missing inhabited cases",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto source = fixture(sources, diagnostics);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto empty = enum_case(source, body, source.empty_case, {});
            const auto arms = std::array {
                PatternCoverageArm {.alternatives = {empty}, .guarded = false},
            };
            const auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(!(coverage->exhaustive));
            ct::expect_equal(coverage->missing_witness, std::string_view(".Pair(false, false)"));
        }
    );

    ct::test(
        "Pattern coverage: a covered product makes independent constraints unreachable",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            constexpr auto width = 32uz;
            auto source = fixture(sources, diagnostics, width);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto any = wildcard(body, source.boolean, source.origin);
            const auto selected = boolean_literal(source, body, false);
            auto arms = std::vector<PatternCoverageArm> {
                {.alternatives = {std::nullopt}, .guarded = false},
            };
            for (auto index = 0uz; index < width; ++index) {
                auto payload = std::vector<PatternID>(width, any);
                payload[index] = selected;
                arms.push_back({
                    .alternatives = {enum_case(source, body, source.pair_case, std::move(payload))},
                    .guarded = false,
                });
            }
            const auto coverage = compute_pattern_coverage(
                source.compilation,
                body.pattern_table(),
                source.pair_enum,
                arms
            );
            if (!ct::expect(coverage.has_value())) {
                return;
            }
            ct::expect(coverage->exhaustive);
            ct::expect(coverage->arm_usefulness.front());
            ct::expect_equal(std::ranges::count(coverage->arm_usefulness, true), 1);
            ct::expect(std::ranges::all_of(coverage->exhaustive_after_arm, std::identity {}));
        }
    );

    ct::test(
        "Pattern coverage: boolean products obey set difference and guarded coverage",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto source = fixture(sources, diagnostics);
            auto body = BodyBuilder(std::move(source.reservation), source.compilation);
            const auto fields = std::array {
                boolean_literal(source, body, false),
                boolean_literal(source, body, true),
                wildcard(body, source.boolean, source.origin),
            };

            struct Selection final {
                PatternID pattern;
                unsigned values;
            };

            auto selections = std::vector<Selection>();
            for (auto left = 0uz; left < fields.size(); ++left) {
                for (auto right = 0uz; right < fields.size(); ++right) {
                    auto values = 0u;
                    for (auto value = 0u; value < 4u; ++value) {
                        if ((left == 2uz || left == (value / 2u))
                            && (right == 2uz || right == (value % 2u))) {
                            values |= 1u << value;
                        }
                    }
                    selections.push_back({
                        .pattern = enum_case(
                            source,
                            body,
                            source.pair_case,
                            {fields[left], fields[right]}
                        ),
                        .values = values,
                    });
                }
            }
            const auto empty = enum_case(source, body, source.empty_case, {});
            for (const auto& first : selections) {
                for (const auto& second : selections) {
                    for (const auto guarded : {false, true}) {
                        const auto arms = std::array {
                            PatternCoverageArm {.alternatives = {empty}, .guarded = false},
                            PatternCoverageArm {
                                .alternatives = {first.pattern},
                                .guarded = guarded
                            },
                            PatternCoverageArm {.alternatives = {second.pattern}, .guarded = false},
                        };
                        const auto coverage = compute_pattern_coverage(
                            source.compilation,
                            body.pattern_table(),
                            source.pair_enum,
                            arms
                        );
                        if (!(ct::expect(coverage.has_value())
                                  .note(
                                      "first.values = ",
                                      first.values,
                                      "second.values = ",
                                      second.values,
                                      "guarded = ",
                                      guarded
                                  ))) {
                            return;
                        }
                        const auto covered = guarded ? 0u : first.values;
                        ct::expect(((coverage->arm_usefulness[2])
                                    == ((second.values & ~covered) != 0u)))
                            .note(
                                "coverage->arm_usefulness[2] == (second.values & ~covered) != 0u",
                                "first.values = ",
                                first.values,
                                "second.values = ",
                                second.values,
                                "guarded = ",
                                guarded
                            );
                        ct::expect(((coverage->exhaustive_after_arm[1]) == (covered == 15u)))
                            .note(
                                "coverage->exhaustive_after_arm[1] == covered == 15u",
                                "first.values = ",
                                first.values,
                                "second.values = ",
                                second.values,
                                "guarded = ",
                                guarded
                            );
                        ct::expect(((coverage->exhaustive) == ((covered | second.values) == 15u)))
                            .note(
                                "coverage->exhaustive == (covered | second.values) == 15u",
                                "first.values = ",
                                first.values,
                                "second.values = ",
                                second.values,
                                "guarded = ",
                                guarded
                            );
                        ct::expect(((coverage->exhaustive_after_arm[2]) == (coverage->exhaustive)))
                            .note(
                                "coverage->exhaustive_after_arm[2] == coverage->exhaustive",
                                "first.values = ",
                                first.values,
                                "second.values = ",
                                second.values,
                                "guarded = ",
                                guarded
                            );
                    }
                }
            }
        }
    );
});

} // namespace
