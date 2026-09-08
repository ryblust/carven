module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.composition;

import :backend.lowering.body.composition;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :test.internal.harness.death;
import std;

namespace {

auto return_statement() noexcept -> TargetStmt {
    return target_lowering_statement(TargetReturnStmt {.expression = std::nullopt});
}

auto unit_type(TargetUnitBuilder& target) noexcept -> TargetTypeID {
    return target.intern_type({
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
        .const_qualified = false,
    });
}
} // namespace

TEST_CASE(
    "Composition: completed evaluation remains composable while termination stops successors"
) {
    auto sequence = LoweringStmtBuilder {};
    auto evaluated = LoweringStmtBuilder {};
    REQUIRE(
        sequence.accept(std::move(evaluated).complete<LoweringCompleted>(LoweringCompleted {}))
    );
    CHECK(sequence.continues());
    const auto exit = LoweringExitTarget {.kind = LoweringExitKind::FunctionReturn, .identity = 0};
    auto terminal = LoweringStmtBuilder {};
    terminal.terminate(return_statement(), exit);
    CHECK_FALSE(sequence.accept(std::move(terminal).complete<LoweringCompleted>(std::nullopt)));
    CHECK_FALSE(sequence.continues());
    CHECK(sequence.exits().contains(exit));
    sequence.emit(return_statement());
    CHECK(std::move(sequence).finish().size() == 1uz);
}

TEST_CASE("Composition: region exits resume only at their own destination") {
    const auto first = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 1};
    const auto second = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 2};
    auto sequence = LoweringStmtBuilder {};
    sequence.terminate(return_statement(), first);
    CHECK_FALSE(sequence.consume_exit(second));
    sequence.resume(TargetIdentifier::from_spelling("done"), TargetJumpRole::RegionExit, first);
    CHECK(sequence.continues());
    CHECK(sequence.exits().targets.empty());
    CHECK(expect_termination("composition.foreign-exit", [=]() noexcept {
        auto foreign = LoweringStmtBuilder {};
        foreign.terminate(return_statement(), first);
        foreign.resume(TargetIdentifier::from_spelling("done"), TargetJumpRole::RegionExit, second);
    }));
}

TEST_CASE("Composition: value lambdas reject external exits") {
    CHECK(expect_termination("composition.external-lambda-exit", []() static noexcept {
        auto target = TargetUnitBuilder {};
        auto sequence = LoweringStmtBuilder {};
        sequence.terminate(
            return_statement(),
            {.kind = LoweringExitKind::FunctionReturn, .identity = 0}
        );
        static_cast<void>(std::move(sequence).result_region(
            unit_type(target),
            {.kind = LoweringExitKind::Value, .identity = 1}
        ));
    }));
    CHECK(expect_termination("composition.missing-normal-result", []() static noexcept {
        auto sequence = LoweringStmtBuilder {};
        static_cast<void>(std::move(sequence).complete<LoweringCompleted>(std::nullopt));
    }));
}
