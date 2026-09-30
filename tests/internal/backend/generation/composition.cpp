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

auto returned(TargetExpr value) noexcept -> std::vector<TargetStmt> {
    auto body = std::vector<TargetStmt>();
    body.push_back(target_lowering_statement(TargetReturnStmt {.expression = std::move(value)}));
    return body;
}

auto boolean_type(TargetUnitBuilder& target) noexcept -> TargetTypeID {
    return target.intern_type({
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
        .const_qualified = false,
    });
}
} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Composition: cleanup obligations end at scopes while declarations retain barriers",
        [] static noexcept {
            auto target = TargetUnitBuilder {};
            const auto variable = [&](std::string_view name) noexcept -> TargetVariableStmt {
                return {
                    .binding = TargetVariableBinding::ConstValue,
                    .maybe_unused = false,
                    .local = target.add_local(TargetIdentifier::from_spelling(name)),
                    .type = boolean_type(target),
                    .initializer = bool_expression(true),
                };
            };
            auto prefix = LoweringStmtBuilder {};
            prefix.declare(variable("plain"), false);
            auto sequence = LoweringStmtBuilder {};
            static_cast<void>(
                sequence.accept(std::move(prefix).complete<LoweringCompleted>(LoweringCompleted {}))
            );
            ct::expect(sequence.owns_storage());
            ct::expect(!sequence.needs_cleanup());

            auto opaque = LoweringStmtBuilder {};
            opaque.emit(target_lowering_statement(variable("unclassified")));
            ct::expect(opaque.needs_cleanup());
            sequence.scope(std::move(opaque));
            ct::expect(!sequence.needs_cleanup());

            auto owned = LoweringStmtBuilder {};
            owned.declare(variable("owned"), true);
            sequence.append(std::move(owned));
            ct::expect(sequence.needs_cleanup());
            auto completed = LoweringStmtBuilder {};
            completed.scope(std::move(sequence));
            ct::expect(!completed.needs_cleanup());
            const auto statements = std::move(completed).finish();
            if (!ct::expect_equal(statements.size(), 1uz)) {
                return;
            }
            ct::expect(std::holds_alternative<TargetBlockStmt>(statements.front().value));
        }
    );

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

    ct::test(
        "Composition: selectable value regions that only return become expressions",
        [] static noexcept {
            auto target = TargetUnitBuilder {};
            const auto yield = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 1};
            const auto selection = [&](LoweringRegionDelivery delivery) noexcept -> TargetExpr {
                auto branches = std::vector<TargetIfBranch>();
                branches.push_back(
                    {.condition = bool_expression(true),
                     .body = returned(
                         {.value = TargetLiteralExpr {
                              .value = TargetIntegerLiteral {
                                  .negative = false,
                                  .magnitude = 1u,
                                  .suffix = TargetIntegerSuffix::None
                              }
                          }}
                     )}
                );
                auto sequence = LoweringStmtBuilder {};
                sequence.terminate(
                    target_lowering_statement(
                        TargetIfStmt {
                            .branches = std::move(branches),
                            .else_body = returned(bool_expression(false))
                        }
                    ),
                    yield
                );
                return std::move(sequence).result_region(boolean_type(target), yield, delivery);
            };
            // A copied result is the selection itself; its integer literal states its type.
            const auto copied = selection(LoweringRegionDelivery::Copied);
            const auto* conditional = std::get_if<TargetConditionalExpr>(&copied.value);
            ct::require(conditional != nullptr);
            ct::expect(
                std::holds_alternative<TargetConstructionExpr>(conditional->true_value->value)
            );
            ct::expect(std::holds_alternative<TargetLiteralExpr>(conditional->false_value->value));
            ct::expect(
                std::holds_alternative<TargetCallExpr>(
                    selection(LoweringRegionDelivery::Factory).value
                )
            );

            // A bound result that names storage is completed as a value.
            const auto local = target.add_local(TargetIdentifier::from_spelling("source"));
            const auto named = [&](LoweringRegionDelivery delivery) noexcept -> TargetExpr {
                auto direct = LoweringStmtBuilder {};
                direct.terminate(
                    target_lowering_statement(
                        TargetReturnStmt {
                            .expression = TargetExpr {.value = TargetLocalExpr {.local = local}}
                        }
                    ),
                    yield
                );
                return std::move(direct).result_region(boolean_type(target), yield, delivery);
            };
            ct::expect(
                std::holds_alternative<TargetLocalExpr>(named(LoweringRegionDelivery::Copied).value)
            );
            const auto bound = named(LoweringRegionDelivery::Bound);
            const auto* snapshot = std::get_if<TargetStaticCastExpr>(&bound.value);
            ct::require(snapshot != nullptr);
            ct::expect(snapshot->type == boolean_type(target));
            ct::expect(std::holds_alternative<TargetLocalExpr>(snapshot->operand->value));
        }
    );

    ct::test("Composition: value regions reject external exits", [] static noexcept {
        ct::expect(expect_termination("composition.external-lambda-exit", []() static noexcept {
            auto target = TargetUnitBuilder {};
            auto sequence = LoweringStmtBuilder {};
            sequence.terminate(
                return_statement(),
                {.kind = LoweringExitKind::FunctionReturn, .identity = 0}
            );
            static_cast<void>(std::move(sequence).result_region(
                boolean_type(target),
                {.kind = LoweringExitKind::Value, .identity = 1},
                LoweringRegionDelivery::Factory
            ));
        }));
        ct::expect(expect_termination("composition.missing-normal-result", []() static noexcept {
            auto sequence = LoweringStmtBuilder {};
            static_cast<void>(std::move(sequence).complete<LoweringCompleted>(std::nullopt));
        }));
        auto target = TargetUnitBuilder {};
        const auto type = boolean_type(target);
        ct::expect(expect_termination("composition.external-direct-value-exit", [=]() noexcept {
            auto sequence = LoweringStmtBuilder {};
            sequence.terminate(
                target_lowering_statement(TargetReturnStmt {.expression = bool_expression(true)}),
                {.kind = LoweringExitKind::FunctionReturn, .identity = 0}
            );
            static_cast<void>(std::move(sequence).result_region(
                type,
                {.kind = LoweringExitKind::Value, .identity = 1},
                LoweringRegionDelivery::Copied
            ));
        }));
    });
});

} // namespace
