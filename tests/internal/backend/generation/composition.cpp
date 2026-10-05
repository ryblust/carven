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

auto boolean_variable(TargetUnitBuilder& target, std::string_view name) noexcept
    -> TargetVariableStmt {
    return {
        .binding = TargetVariableBinding::ConstValue,
        .maybe_unused = false,
        .local = target.add_local(TargetIdentifier::from_spelling(name)),
        .type = boolean_type(target),
        .initializer = bool_expression(true),
    };
}

const TestSuite suite([] static noexcept {
    "Composition: cleanup obligations end at scopes while declarations retain barriers"_test =
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
            expect(sequence.owns_storage());
            expect(!sequence.needs_cleanup());

            auto opaque = LoweringStmtBuilder {};
            opaque.emit(target_lowering_statement(variable("unclassified")));
            expect(opaque.needs_cleanup());
            sequence.scope(std::move(opaque));
            expect(!sequence.needs_cleanup());

            auto owned = LoweringStmtBuilder {};
            owned.declare(variable("owned"), true);
            sequence.append(std::move(owned));
            expect(sequence.needs_cleanup());
            auto completed = LoweringStmtBuilder {};
            completed.scope(std::move(sequence));
            expect(!completed.needs_cleanup());
            const auto statements = std::move(completed).finish();
            if (!expect_equal(statements.size(), 1uz)) {
                return;
            }
            expect(std::holds_alternative<TargetBlockStmt>(statements.front().value));
        };

    "Composition: completed evaluation remains composable while termination stops successors"_test =
        [] static noexcept {
            auto sequence = LoweringStmtBuilder {};
            auto evaluated = LoweringStmtBuilder {};
            if (!expect(
                    sequence
                        .accept(
                            std::move(evaluated).complete<LoweringCompleted>(LoweringCompleted {})
                        )
                        .has_value()
                )) {
                return;
            }
            expect(sequence.continues());
            const auto exit =
                LoweringExitTarget {.kind = LoweringExitKind::FunctionReturn, .identity = 0};
            auto terminal = LoweringStmtBuilder {};
            terminal.terminate(return_statement(), exit);
            expect(!sequence.accept(std::move(terminal).complete<LoweringCompleted>(std::nullopt))
                        .has_value());
            expect(!(sequence.continues()));
            expect(sequence.exits().contains(exit));
            sequence.emit(return_statement());
            expect(std::move(sequence).finish().size() == 1uz);
        };

    "Composition: child exits retain cleanup owed by either open scope"_test = [] static noexcept {
        enum class Composition { Append, Accept, Scope };
        struct Scenario final {
            const char* name;
            Composition composition;
            bool parent_cleanup;
            bool child_cleanup;
        };
        const auto scenarios = std::array {
            Scenario {
                .name = "append crosses parent cleanup",
                .composition = Composition::Append,
                .parent_cleanup = true,
                .child_cleanup = false
            },
            Scenario {
                .name = "accept crosses parent cleanup",
                .composition = Composition::Accept,
                .parent_cleanup = true,
                .child_cleanup = false
            },
            Scenario {
                .name = "scope crosses parent cleanup",
                .composition = Composition::Scope,
                .parent_cleanup = true,
                .child_cleanup = false
            },
            Scenario {
                .name = "append retains child cleanup",
                .composition = Composition::Append,
                .parent_cleanup = false,
                .child_cleanup = true
            },
            Scenario {
                .name = "accept retains child cleanup",
                .composition = Composition::Accept,
                .parent_cleanup = false,
                .child_cleanup = true
            },
            Scenario {
                .name = "scope retains child cleanup",
                .composition = Composition::Scope,
                .parent_cleanup = false,
                .child_cleanup = true
            },
        };
        each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
            auto target = TargetUnitBuilder {};
            const auto exit = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 1};
            auto parent = LoweringStmtBuilder {};
            parent.declare(boolean_variable(target, "parent"), scenario.parent_cleanup);
            auto child = LoweringStmtBuilder {};
            child.declare(boolean_variable(target, "child"), scenario.child_cleanup);
            child.terminate(return_statement(), exit);
            expect_equal(child.exits().crosses_cleanup(exit), scenario.child_cleanup);

            switch (scenario.composition) {
                case Composition::Append: parent.append(std::move(child)); break;
                case Composition::Accept:
                    expect(!parent
                                .accept(std::move(child).complete<LoweringCompleted>(std::nullopt))
                                .has_value());
                    break;
                case Composition::Scope: parent.scope(std::move(child)); break;
            }
            expect(!parent.continues());
            expect(parent.exits().contains(exit));
            expect(parent.exits().crosses_cleanup(exit));
            expect(parent.consume_exit(exit));
            expect(!parent.exits().contains(exit));
            expect(!parent.exits().crosses_cleanup(exit));
        });
    };

    "Composition: completed child cleanup does not mark a later exit"_test = [] static noexcept {
        auto target = TargetUnitBuilder {};
        auto child = LoweringStmtBuilder {};
        child.declare(boolean_variable(target, "closed"), true);
        auto parent = LoweringStmtBuilder {};
        parent.scope(std::move(child));
        expect(parent.continues());
        expect(!parent.needs_cleanup());
        const auto exit = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 1};
        parent.terminate(return_statement(), exit);
        expect(parent.exits().contains(exit));
        expect(!parent.exits().crosses_cleanup(exit));
    };

    "Composition: cleanup facts follow the source order of each exit"_test = [] static noexcept {
        auto target = TargetUnitBuilder {};
        const auto early = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 1};
        const auto late = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 2};
        auto parent = LoweringStmtBuilder {};
        const auto branch = [&](LoweringExitTarget exit) noexcept {
            auto selected = LoweringStmtBuilder {};
            selected.terminate(return_statement(), exit);
            parent.record_exits(selected.exits());
            auto branches = std::vector<TargetIfBranch>();
            branches.push_back(
                {.condition = bool_expression(true), .body = std::move(selected).finish()}
            );
            parent.emit(target_lowering_statement(
                TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
            ));
        };
        branch(early);
        parent.declare(boolean_variable(target, "later"), true);
        expect(parent.needs_cleanup());
        expect(!parent.exits().crosses_cleanup(early));
        branch(late);
        expect(parent.exits().crosses_cleanup(late));
        expect(!parent.exits().crosses_cleanup(early));

        // Another path to the same receiver can owe cleanup even when the first did not.
        branch(early);
        expect(parent.exits().crosses_cleanup(early));
        expect(parent.consume_exit(early));
        expect(!parent.exits().crosses_cleanup(early));
        expect(parent.exits().crosses_cleanup(late));
        expect(parent.consume_exit(late));
        expect(parent.exits().entries.empty());
    };

    "Composition: replacing an exit uses its original cleanup boundary"_test = [] static noexcept {
        auto target = TargetUnitBuilder {};
        const auto early = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 1};
        const auto late = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 2};
        const auto outward = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 3};
        const auto owned = LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 4};
        const auto late_outward =
            LoweringExitTarget {.kind = LoweringExitKind::Failure, .identity = 5};
        auto parent = LoweringStmtBuilder {};
        auto early_branch = LoweringStmtBuilder {};
        early_branch.terminate(return_statement(), early);
        parent.record_exits(early_branch.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = bool_expression(true), .body = std::move(early_branch).finish()}
        );
        parent.emit(target_lowering_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
        parent.declare(boolean_variable(target, "later"), true);
        parent.terminate(return_statement(), late);
        expect(parent.needs_cleanup());
        expect(!parent.exits().crosses_cleanup(early));
        expect(parent.exits().crosses_cleanup(late));

        const auto continuation = LoweringExitSummary {
            .entries = {
                {.target = outward, .needs_cleanup = false},
                {.target = owned, .needs_cleanup = true},
            },
        };
        parent.replace_exit(early, continuation);
        expect(!parent.exits().contains(early));
        expect(!parent.exits().crosses_cleanup(outward));
        expect(parent.exits().crosses_cleanup(owned));
        parent.replace_exit(
            late,
            LoweringExitSummary {.entries = {{.target = late_outward, .needs_cleanup = false}}}
        );
        expect(!parent.exits().contains(late));
        expect(!parent.exits().crosses_cleanup(late));
        expect(parent.exits().crosses_cleanup(late_outward));
    };

    "Composition: region exits resume only at their own destination"_test = [] static noexcept {
        const auto first = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 1};
        const auto second = LoweringExitTarget {.kind = LoweringExitKind::Value, .identity = 2};
        auto sequence = LoweringStmtBuilder {};
        sequence.terminate(return_statement(), first);
        expect(!(sequence.consume_exit(second)));
        sequence.resume(TargetIdentifier::from_spelling("done"), TargetJumpRole::RegionExit, first);
        expect(sequence.continues());
        expect(sequence.exits().entries.empty());
        expect(expect_termination("composition.foreign-exit", [=]() noexcept {
            auto foreign = LoweringStmtBuilder {};
            foreign.terminate(return_statement(), first);
            foreign.resume(
                TargetIdentifier::from_spelling("done"),
                TargetJumpRole::RegionExit,
                second
            );
        }));
    };

    "Composition: selectable value regions that only return become expressions"_test =
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
            require(conditional != nullptr);
            expect(std::holds_alternative<TargetConstructionExpr>(conditional->true_value->value));
            expect(std::holds_alternative<TargetLiteralExpr>(conditional->false_value->value));
            expect(
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
            expect(
                std::holds_alternative<TargetLocalExpr>(named(LoweringRegionDelivery::Copied).value)
            );
            const auto bound = named(LoweringRegionDelivery::Bound);
            const auto* snapshot = std::get_if<TargetStaticCastExpr>(&bound.value);
            require(snapshot != nullptr);
            expect(snapshot->type == boolean_type(target));
            expect(std::holds_alternative<TargetLocalExpr>(snapshot->operand->value));
        };

    "Composition: value regions reject external exits"_test = [] static noexcept {
        expect(expect_termination("composition.external-lambda-exit", []() static noexcept {
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
        expect(expect_termination("composition.missing-normal-result", []() static noexcept {
            auto sequence = LoweringStmtBuilder {};
            static_cast<void>(std::move(sequence).complete<LoweringCompleted>(std::nullopt));
        }));
        auto target = TargetUnitBuilder {};
        const auto type = boolean_type(target);
        expect(expect_termination("composition.external-direct-value-exit", [=]() noexcept {
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
    };
});

} // namespace
