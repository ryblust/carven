module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.lowering.stmt;

import :backend.lowering.stmt;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.origin;
import :backend.target.stmt;
import std;

namespace {

auto source_attribution() noexcept -> TargetAttribution {
    return {
        .kind = TargetAttributionKind::SourceOwned,
        .origin = TargetSourceOrigin {.display_origin = "fixture.cv", .line = 1},
        .reason = std::nullopt,
    };
}

static_assert(!std::copy_constructible<PreparedTargetStatement>);
static_assert(std::move_constructible<PreparedTargetStatement>);
static_assert(!std::is_move_assignable_v<PreparedTargetStatement>);

} // namespace

TEST_CASE("Prepared target statement: move and initializer classification consume once") {
    auto source = PreparedTargetStatement(
        TargetStmt {
            .value = TargetExprStmt {.expression = TargetExprID::from_index(3)},
            .attribution = source_attribution(),
        }
    );
    auto moved = PreparedTargetStatement(std::move(source));
    auto classified = std::move(moved).classify_for_initializer();

    REQUIRE(classified.is_for_initializer());
    auto initializer = std::move(classified).take_for_initializer();
    const auto* expression = std::get_if<TargetExprStmt>(&initializer.value);
    REQUIRE(expression != nullptr);
    CHECK_EQ(expression->expression.index(), 3u);
}

TEST_CASE("Prepared target statement: step classification preserves its legal variant") {
    auto classified = PreparedTargetStatement(
                          TargetStmt {
                              .value =
                                  TargetUpdateStmt {
                                      .op = TargetUpdateOperator::Increment,
                                      .target = TargetExprID::from_index(5),
                                  },
                              .attribution = source_attribution(),
                          }
    )
                          .classify_for_step();

    REQUIRE(classified.is_for_step());
    auto step = std::move(classified).take_for_step();
    const auto* update = std::get_if<TargetUpdateStmt>(&step.value);
    REQUIRE(update != nullptr);
    CHECK_EQ(update->target.index(), 5u);
}
