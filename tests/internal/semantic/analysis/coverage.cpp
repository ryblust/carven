module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.coverage;

import :compiler.request;
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
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
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

auto fixture(SourceManager& sources, DiagnosticSink& diagnostics) noexcept -> CoverageFixture {
    const auto source = sources.append_virtual("coverage-fixture.cv", "");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path("coverage.fixture"),
        },
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto compilation = ProgramDraft::begin(std::move(*syntax), diagnostics);
    const auto provenance_module = compilation.provenance_module_at(0uz);
    const auto source_id = compilation.module_source(provenance_module);
    const auto origin = compilation.append_source_origin(source_id, Span::at(0u));
    const auto boolean = compilation.intern_builtin_type(BuiltinType::Bool);

    const auto module_id = compilation.reserve_module_declaration();
    const auto enumeration = compilation.reserve_enum_declaration();
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
        pair_case,
        ConstructionEnumCaseDeclaration {
            .owner = enumeration,
            .name = compilation.intern_spelling("Pair"),
            .origin = origin,
            .payload_types = {boolean, boolean},
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
            .cases = {pair_case, empty_case},
            .capabilities = NominalCapabilities {.equality = true},
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
            .capabilities = NominalCapabilities {.equality = true},
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
    compilation.finish_declarations();

    auto reservation = compilation.reserve_body(BodyKind::Test);
    compilation.define_test(
        test,
        TestDeclaration {
            .module_id = module_id,
            .name = compilation.intern_spelling("coverage"),
            .origin = origin,
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

TEST_CASE("Pattern coverage: enum payload overlap and guarded exhaustiveness stay exact") {
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
    auto coverage =
        compute_pattern_coverage(source.compilation, body.pattern_table(), source.pair_enum, arms);
    REQUIRE(coverage.has_value());
    CHECK((coverage->arm_usefulness == std::vector<bool> {true, true, true, true, false}));
    CHECK((coverage->exhaustive_after_arm == std::vector<bool> {false, false, false, true, true}));
    CHECK(coverage->exhaustive);
    CHECK(coverage->redundant_alternatives.empty());
    CHECK(diagnostics.empty());
}

TEST_CASE("Pattern coverage: redundant alternatives and finite witnesses are reported") {
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
    auto coverage =
        compute_pattern_coverage(source.compilation, body.pattern_table(), source.boolean, arms);
    REQUIRE(coverage.has_value());
    CHECK_FALSE(coverage->exhaustive);
    CHECK_EQ(coverage->missing_witness, "false");
    REQUIRE_EQ(coverage->redundant_alternatives.size(), 1uz);
    CHECK_EQ(coverage->redundant_alternatives.front().arm, 0uz);
    CHECK_EQ(coverage->redundant_alternatives.front().alternative, 1uz);
    CHECK(diagnostics.empty());
}

TEST_CASE("Pattern coverage: an alternative covered by a union is redundant") {
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
    auto coverage =
        compute_pattern_coverage(source.compilation, body.pattern_table(), source.pair_enum, arms);
    REQUIRE(coverage.has_value());
    REQUIRE_EQ(coverage->redundant_alternatives.size(), 1uz);
    CHECK_EQ(coverage->redundant_alternatives.front().arm, 0uz);
    CHECK_EQ(coverage->redundant_alternatives.front().alternative, 0uz);
    CHECK_FALSE(coverage->alternative_usefulness.front().front());
    CHECK(diagnostics.empty());
}

TEST_CASE("Pattern coverage: witnesses retain missing nested payload constructors") {
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
    auto coverage =
        compute_pattern_coverage(source.compilation, body.pattern_table(), source.pair_enum, arms);
    REQUIRE(coverage.has_value());
    CHECK_FALSE(coverage->exhaustive);
    CHECK_EQ(coverage->missing_witness, ".Pair(false, false)");
    CHECK(diagnostics.empty());
}

TEST_CASE("Pattern coverage: an enum with no constructors is exhaustive") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto source = fixture(sources, diagnostics);
    const auto body = BodyBuilder(std::move(source.reservation), source.compilation);
    const auto arms = std::array<PatternCoverageArm, 0> {};
    auto coverage =
        compute_pattern_coverage(source.compilation, body.pattern_table(), source.empty_enum, arms);
    REQUIRE(coverage.has_value());
    CHECK(coverage->exhaustive);
    CHECK(coverage->arm_usefulness.empty());
    CHECK(diagnostics.empty());
}
