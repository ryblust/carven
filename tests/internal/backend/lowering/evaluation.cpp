module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.lowering.evaluation;

import :backend.lowering.expr;
import :semantic.hir.place;
import std;

namespace {

auto effect(
    std::vector<SymbolID> reads = {},
    std::vector<SymbolID> writes = {},
    std::vector<SymbolID> takes = {}
) noexcept -> EvaluationEffect {
    return {
        .reads = std::move(reads),
        .writes = std::move(writes),
        .takes = std::move(takes),
        .opaque_boundary = false,
    };
}

} // namespace

TEST_CASE("Backend evaluation: ordered effect spans determine commutativity without unions") {
    const auto first = SymbolID::from_index(0);
    const auto second = SymbolID::from_index(1);

    CHECK(
        TargetEvaluationSequencer::effects_commute(effect({first}), false, effect({first}), false)
    );
    CHECK(
        TargetEvaluationSequencer::effects_commute(effect({first}), false, effect({second}), false)
    );
    CHECK_FALSE(
        TargetEvaluationSequencer::effects_commute(
            effect({}, {first}),
            false,
            effect({first}),
            false
        )
    );
    CHECK_FALSE(
        TargetEvaluationSequencer::effects_commute(
            effect({}, {}, {first}),
            false,
            effect({}, {first}),
            false
        )
    );
    CHECK_FALSE(
        TargetEvaluationSequencer::effects_commute(
            effect({first, second}),
            false,
            effect({}, {}, {second}),
            false
        )
    );
}

TEST_CASE("Backend evaluation: control effects commute only with observational reads") {
    const auto place = SymbolID::from_index(0);
    const auto empty = effect();
    const auto read = effect({place});
    const auto write = effect({}, {place});
    const auto take = effect({}, {}, {place});

    auto opaque = effect();
    opaque.opaque_boundary = true;
    CHECK(TargetEvaluationSequencer::effects_commute(opaque, false, empty, false));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(opaque, false, read, false));

    const auto terminating = effect();
    CHECK(TargetEvaluationSequencer::effects_commute(terminating, true, read, false));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(terminating, true, write, false));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(terminating, true, take, false));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(terminating, true, terminating, true));

    auto conflicting_opaque = opaque;
    conflicting_opaque.writes = {place};
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(conflicting_opaque, false, read, false));
}
