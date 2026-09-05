module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.target.verify;

import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import :backend.target;
import :test.internal.backend.target.fixture;
import :test.internal.harness.death;
import std;

namespace {

auto identifier(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto attribution() noexcept -> TargetAttribution {
    return TargetGeneratedExpansionAttribution {
        .reason = TargetExpansionReason::LoweringSupport,
    };
}

auto literal(bool value = true) noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = value}};
}

auto one_statement(TargetStmt statement) noexcept -> std::vector<TargetStmt> {
    auto result = std::vector<TargetStmt>();
    result.push_back(std::move(statement));
    return result;
}

auto one_item(TargetItem item) noexcept -> std::vector<TargetItem> {
    auto result = std::vector<TargetItem>();
    result.push_back(std::move(item));
    return result;
}

auto sections(std::vector<TargetItem> body = {}) noexcept -> TargetUnitSections {
    return {.preamble = {}, .body = std::move(body), .epilogue = {}};
}

auto bool_type() noexcept -> TargetType {
    return {
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::Bool,
                .type_argument_ids = {},
            },
        .const_qualified = false,
    };
}

auto function(TargetTypeID result, std::vector<TargetStmt> body) noexcept -> TargetItem {
    return {
        .value = TargetDecl {TargetFunctionDecl {
            .name = TargetName(identifier("fixture")),
            .parameters = {},
            .result = result,
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .static_specifier = false,
            .inline_specifier = false,
        }},
        .attribution = attribution(),
    };
}

auto require_violation(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& unit_sections,
    TargetSealViolationKind kind
) noexcept -> void {
    const auto result = TargetTestingFixture::validate_unit(identity, types, unit_sections);
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, kind);
}

static_assert(std::is_aggregate_v<TargetExpr>);
static_assert(std::is_aggregate_v<TargetStmt>);
static_assert(std::is_aggregate_v<TargetItem>);
static_assert(!std::copy_constructible<TargetExpr>);
static_assert(!std::copy_constructible<TargetStmt>);
static_assert(std::move_constructible<TargetExpr>);
static_assert(std::move_constructible<TargetStmt>);
static_assert(std::move_constructible<TargetItem>);
static_assert(!std::constructible_from<TargetUnitBuilder, TargetArtifactID>);
static_assert(!std::constructible_from<TargetTypeID, TargetUnitIdentity, std::uint32_t>);
static_assert(!std::is_move_assignable_v<TargetUnit>);
static_assert(std::ranges::range<TargetPlanTableEntries<int, TargetArtifactID>>);

} // namespace

TEST_CASE("Target type construction: children already belong to the unit") {
    auto builder = TargetUnitBuilder();
    const auto foreign = TargetTestingFixture::unit_identity();
    const auto invalid = std::array {
        TargetTestingFixture::type_id(foreign, 0),
        TargetTestingFixture::type_id(builder.identity(), 0),
        TargetTestingFixture::type_id(builder.identity(), 7),
    };
    for (const auto child : invalid) {
        CHECK(expect_termination("target-type-child", [&] noexcept {
            static_cast<void>(builder.intern_type(
                TargetType {
                    .value =
                        TargetArrayType {
                            .element_type_id = child,
                            .extent = TargetArrayExtent {.magnitude = 2}
                        },
                    .const_qualified = false,
                }
            ));
        }));
    }
    const auto element = builder.intern_type(bool_type());
    const auto array = builder.intern_type(
        TargetType {
            .value =
                TargetArrayType {
                    .element_type_id = element,
                    .extent = TargetArrayExtent {.magnitude = 2}
                },
            .const_qualified = false,
        }
    );
    CHECK_EQ(std::get<TargetArrayType>(builder.copy_type(array).value).element_type_id, element);
}

TEST_CASE("Target jump verifier: entering an empty nested scope is legal") {
    const auto owner = TargetTestingFixture::unit_identity();
    const auto type = TargetTestingFixture::type_id(owner, 0);
    auto body = std::vector<TargetStmt>();
    body.push_back({
        .value =
            TargetGotoStmt {
                .label = identifier("inside"),
                .role = TargetJumpRole::RegionExit,
            },
        .attribution = attribution(),
    });
    body.push_back({
        .value =
            TargetBlockStmt {
                .statements = one_statement(
                    TargetStmt {
                        .value =
                            TargetLabelStmt {
                                .label = identifier("inside"),
                                .role = TargetJumpRole::RegionExit,
                            },
                        .attribution = attribution(),
                    }
                ),
                .scoped = true,
            },
        .attribution = attribution(),
    });
    const auto types = std::array {bool_type()};
    CHECK(
        TargetTestingFixture::validate_unit(
            owner,
            types,
            sections(one_item(function(type, std::move(body))))
        )
            .has_value()
    );
}

TEST_CASE("Target jump verifier: region exit cannot jump backward") {
    const auto owner = TargetTestingFixture::unit_identity();
    const auto type = TargetTestingFixture::type_id(owner, 0);
    auto body = std::vector<TargetStmt>();
    body.push_back({
        .value =
            TargetLabelStmt {
                .label = identifier("loop"),
                .role = TargetJumpRole::RegionExit,
            },
        .attribution = attribution(),
    });
    body.push_back({
        .value =
            TargetGotoStmt {
                .label = identifier("loop"),
                .role = TargetJumpRole::RegionExit,
            },
        .attribution = attribution(),
    });
    const auto types = std::array {bool_type()};
    CHECK_FALSE(
        TargetTestingFixture::validate_unit(
            owner,
            types,
            sections(one_item(function(type, std::move(body))))
        )
            .has_value()
    );
}

TEST_CASE("Target jump verifier: entering past initialization is rejected") {
    const auto owner = TargetTestingFixture::unit_identity();
    const auto type = TargetTestingFixture::type_id(owner, 0);
    auto nested = std::vector<TargetStmt>();
    nested.push_back({
        .value =
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = identifier("value"),
                .type = type,
                .initializer = literal(),
            },
        .attribution = attribution(),
    });
    nested.push_back({
        .value =
            TargetLabelStmt {
                .label = identifier("inside"),
                .role = TargetJumpRole::RegionExit,
            },
        .attribution = attribution(),
    });
    auto body = std::vector<TargetStmt>();
    body.push_back({
        .value =
            TargetGotoStmt {
                .label = identifier("inside"),
                .role = TargetJumpRole::RegionExit,
            },
        .attribution = attribution(),
    });
    body.push_back({
        .value =
            TargetBlockStmt {
                .statements = std::move(nested),
                .scoped = true,
            },
        .attribution = attribution(),
    });
    const auto types = std::array {bool_type()};
    require_violation(
        owner,
        types,
        sections(one_item(function(type, std::move(body)))),
        TargetSealViolationKind::InvalidControl
    );
}

TEST_CASE("Target builder: interning is unit-owned and unused types add no dependency") {
    auto builder = TargetTestingFixture::unit_builder();
    static_cast<void>(builder.intern_type(
        TargetType {
            .value =
                TargetIntrinsicType {
                    .symbol = TargetSymbol::RuntimeFunctionRef,
                    .type_argument_ids = {},
                },
            .const_qualified = false,
        }
    ));
    const auto first = builder.intern_type(bool_type());
    const auto second = builder.intern_type(bool_type());
    const auto unit = std::move(builder).finish(sections(one_item(function(first, {}))));

    CHECK_EQ(first, second);
    CHECK_EQ(unit.type_count(), 2);
    CHECK(unit.directive_groups().empty());
}

TEST_CASE("Target unit builder: move and finish poison the source capability") {
    auto source = TargetTestingFixture::unit_builder();
    auto moved = TargetUnitBuilder(std::move(source));
    CHECK(expect_termination(
        "target-unit-builder-moved-source",
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the moved-from contract.
        [&] { static_cast<void>(source.intern_type(bool_type())); }
    ));

    auto unit = std::move(moved).finish(sections());
    CHECK(expect_termination(
        "target-unit-builder-finished-source",
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the consumed-builder contract.
        [&] { static_cast<void>(moved.intern_type(bool_type())); }
    ));

    const auto moved_unit = TargetUnit(std::move(unit));
    CHECK_EQ(moved_unit.type_count(), 0);
    CHECK(expect_termination(
        "target-unit-moved-source",
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the moved-from contract.
        [&] { static_cast<void>(unit.type_count()); }
    ));
}

TEST_CASE("Target plan table builder: move and seal poison the source capability") {
    auto source =
        TargetPlanTableBuilder<int, TargetArtifactID>(TargetTestingFixture::plan_identity());
    static_cast<void>(source.add(1));
    auto moved = TargetPlanTableBuilder<int, TargetArtifactID>(std::move(source));
    CHECK(expect_termination(
        "target-plan-table-builder-moved-source",
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the moved-from contract.
        [&] { static_cast<void>(source.add(2)); }
    ));

    const auto table = std::move(moved).seal();
    CHECK_EQ(table.size(), 1);
    CHECK_EQ(std::ranges::distance(table.entries()), 1);
    CHECK(expect_termination(
        "target-plan-table-builder-sealed-source",
        // NOLINTNEXTLINE(bugprone-use-after-move): exercises the consumed-builder contract.
        [&] { static_cast<void>(moved.add(3)); }
    ));
}
