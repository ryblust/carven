module carven:test.internal.backend.generation.composition;

import :backend.realization.composition;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Composition: completed evaluation remains composable while termination stops successors",
        [] static noexcept {
            auto sequence = LoweringStmtBuilder {};
            auto evaluated = LoweringStmtBuilder {};
            if (!ct::expect(
                    sequence
                        .accept(
                            std::move(evaluated).complete<LoweringCompleted>(LoweringCompleted {})
                        )
                        .has_value()
                )) {
                return;
            }
            ct::expect(sequence.continues());
            const auto exit =
                LoweringExitTarget {.kind = LoweringExitKind::FunctionReturn, .identity = 0};
            auto terminal = LoweringStmtBuilder {};
            terminal.terminate(return_statement(), exit);
            ct::expect(!sequence
                            .accept(std::move(terminal).complete<LoweringCompleted>(std::nullopt))
                            .has_value());
            ct::expect(!(sequence.continues()));
            ct::expect(sequence.exits().contains(exit));
            sequence.emit(return_statement());
            ct::expect(std::move(sequence).finish().size() == 1uz);
        }
    );

    ct::test("Composition: region exits resume only at their own destination", [] static noexcept {
        const auto first = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 1};
        const auto second = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 2};
        auto sequence = LoweringStmtBuilder {};
        sequence.terminate(return_statement(), first);
        ct::expect(!(sequence.consume_exit(second)));
        sequence.resume(TargetIdentifier::from_spelling("done"), TargetJumpRole::RegionExit, first);
        ct::expect(sequence.continues());
        ct::expect(sequence.exits().targets.empty());
        ct::expect(expect_termination("composition.foreign-exit", [=]() noexcept {
            auto foreign = LoweringStmtBuilder {};
            foreign.terminate(return_statement(), first);
            foreign.resume(
                TargetIdentifier::from_spelling("done"),
                TargetJumpRole::RegionExit,
                second
            );
        }));
    });

    ct::test("Composition: value lambdas reject external exits", [] static noexcept {
        ct::expect(expect_termination("composition.external-lambda-exit", []() static noexcept {
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
        ct::expect(expect_termination("composition.missing-normal-result", []() static noexcept {
            auto sequence = LoweringStmtBuilder {};
            static_cast<void>(std::move(sequence).complete<LoweringCompleted>(std::nullopt));
        }));
    });
});

} // namespace
