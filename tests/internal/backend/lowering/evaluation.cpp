module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.lowering.evaluation;

import :backend.lowering.expressions;
import :semantic.hir.place;
import std;

namespace {

auto effect(
    std::vector<SemanticPlaceID> reads = {},
    std::vector<SemanticPlaceID> writes = {},
    std::vector<SemanticPlaceID> takes = {}
) noexcept -> EvaluationEffect {
    return {
        .reads = std::move(reads),
        .writes = std::move(writes),
        .takes = std::move(takes),
        .opaque_boundary = false,
        .may_terminate = false,
    };
}

} // namespace

TEST_CASE("Backend evaluation: ordered effect spans determine commutativity without unions") {
    const auto first = SemanticPlaceID::from_index(0);
    const auto second = SemanticPlaceID::from_index(1);

    CHECK(TargetEvaluationSequencer::effects_commute(effect({first}), effect({first})));
    CHECK(TargetEvaluationSequencer::effects_commute(effect({first}), effect({second})));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(effect({}, {first}), effect({first})));
    CHECK_FALSE(
        TargetEvaluationSequencer::effects_commute(effect({}, {}, {first}), effect({}, {first}))
    );
    CHECK_FALSE(
        TargetEvaluationSequencer::effects_commute(
            effect({first, second}),
            effect({}, {}, {second})
        )
    );
}

TEST_CASE("Backend evaluation: control effects commute only with observational reads") {
    const auto place = SemanticPlaceID::from_index(0);
    const auto empty = effect();
    const auto read = effect({place});
    const auto write = effect({}, {place});
    const auto take = effect({}, {}, {place});

    auto opaque = effect();
    opaque.opaque_boundary = true;
    CHECK(TargetEvaluationSequencer::effects_commute(opaque, empty));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(opaque, read));

    auto terminating = effect();
    terminating.may_terminate = true;
    CHECK(TargetEvaluationSequencer::effects_commute(terminating, read));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(terminating, write));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(terminating, take));
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(terminating, terminating));

    auto conflicting_opaque = opaque;
    conflicting_opaque.writes = {place};
    CHECK_FALSE(TargetEvaluationSequencer::effects_commute(conflicting_opaque, read));
}
