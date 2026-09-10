module carven:backend.realization.stmt.impl;

import :backend.construction;
import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyRealizer::emit_return(
    std::optional<TargetExpr> value,
    LoweringStmtBuilder& destination,
    const LoweringResultDestination& result
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (std::holds_alternative<LoweringReturnResult>(result)) {
        if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit)) {
            const auto& signature =
                context.semantic().callable_signatures().signature(callable->signature);
            if (!context.plan().failure_abi().members(signature.failures).empty()) {
                auto arguments = std::vector<TargetExpr>();
                if (value.has_value()) {
                    if (context.is_void(signature.result)) {
                        destination.emit(statement_expression(std::move(*value)));
                    } else {
                        auto body = std::vector<TargetStmt>();
                        body.push_back(
                            generated_statement(TargetReturnStmt {.expression = std::move(*value)})
                        );
                        arguments.push_back(
                            TargetExpr {
                                .value = TargetLambdaExpr {
                                    .parameters = {},
                                    .result = context.lower_type(signature.result),
                                    .body = std::move(body)
                                }
                            }
                        );
                    }
                }
                value = call_expression(
                    static_member_expression(
                        context.outcome_type(callable->signature),
                        TargetIdentifier::from_spelling(
                            context.is_void(signature.result) ? "success" : "success_from"
                        )
                    ),
                    std::move(arguments)
                );
            }
        }
    }
    destination.terminate(
        generated_statement(TargetReturnStmt {.expression = std::move(value)}),
        std::holds_alternative<LoweringYieldResult>(result)
            ? std::get<LoweringYieldResult>(result).target
            : LoweringExitTarget {LoweringExitKind::FunctionReturn, 0}
    );
}

auto BodyRealizer::emit_failure(
    TargetExpr value,
    const ConstructionFailureExit& exit,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (const auto* handler = std::get_if<ConstructionHandlerExit>(&exit)) {
        const auto& failure_destination = handlers.at(handler->handler);
        destination.emit(statement_expression(call_member(
            name_expression(failure_destination.storage),
            "emplace",
            target_expressions(std::move(value))
        )));
        destination.terminate(
            generated_statement(
                TargetGotoStmt {
                    .label = failure_destination.label,
                    .role = TargetJumpRole::FailureTransfer
                }
            ),
            failure_destination.target
        );
        return;
    }
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    if (callable == nullptr) {
        invariant_violation("failure escaped a test body");
    }
    destination.terminate(
        generated_statement(
            TargetReturnStmt {
                .expression = call_expression(
                    static_member_expression(
                        context.outcome_type(callable->signature),
                        TargetIdentifier::from_spelling("failure")
                    ),
                    target_expressions(std::move(value))
                )
            }
        ),
        LoweringExitTarget {LoweringExitKind::Failure, 0}
    );
}

auto BodyRealizer::transfer_failure(
    const TargetIdentifier& storage,
    FailureSetID failures,
    const ConstructionFailureExit& exit,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    destination.scope(dispatch_failure(VariantFailureSource {.storage = storage}, failures, exit));
}

auto BodyRealizer::dispatch_failure(
    const FailureSource& source,
    FailureSetID failures,
    const ConstructionFailureExit& exit
) noexcept -> LoweringStmtBuilder {
    auto transfers = LoweringStmtBuilder();
    for (const auto type : context.plan().failure_abi().members(failures)) {
        const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
        transfers.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = projection,
                .type = context.pointer_type(context.intrinsic_type(TargetSymbol::Auto)),
                .initializer = std::visit(
                    Overloaded {
                        [&](const OutcomeFailureSource& outcome) noexcept {
                            auto storage = name_expression(outcome.storage);
                            if (outcome.deferred) {
                                storage = dereference_expression(std::move(storage));
                            }
                            return template_call_expression(
                                member_expression(
                                    std::move(storage),
                                    TargetIdentifier::from_spelling("failure_if")
                                ),
                                {context.lower_type(type)},
                                {}
                            );
                        },
                        [&](const VariantFailureSource& variant) noexcept {
                            return template_call_expression(
                                intrinsic_expression(TargetSymbol::StdGetIf),
                                {context.lower_type(type)},
                                target_expressions(address_expression(
                                    dereference_expression(name_expression(variant.storage))
                                ))
                            );
                        }
                    },
                    source
                )
            }
        ));
        auto transfer = LoweringStmtBuilder();
        emit_failure(
            transfer_expression(dereference_expression(name_expression(projection))),
            exit,
            transfer
        );
        transfers.record_exits(transfer.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = name_expression(projection), .body = std::move(transfer).finish()}
        );
        transfers.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    transfers.terminate(
        generated_statement(TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}),
        LoweringExitTarget {LoweringExitKind::Unreachable, 0}
    );
    return transfers;
}

auto BodyRealizer::result_expression(
    ConstructionExpressionID source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (std::holds_alternative<ConstructionConditional>(construction.expression(source).value)
        || std::holds_alternative<ConstructionMatch>(construction.expression(source).value)
        || std::holds_alternative<ConstructionTry>(construction.expression(source).value)) {
        structured_delivery(source, result, destination);
        return;
    }
    if (std::holds_alternative<LoweringDiscardResult>(result)) {
        static_cast<void>(destination.accept(discard(source)));
        return;
    }
    auto literal = RealizationLiteralContext::Exact;
    if (returns_result(result)) {
        if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
            std::holds_alternative<LoweringYieldResult>(result)
            || (callable != nullptr
                && context.plan()
                       .failure_abi()
                       .members(context.semantic()
                                    .callable_signatures()
                                    .signature(callable->signature)
                                    .failures)
                       .empty())) {
            literal = RealizationLiteralContext::TargetTyped;
        }
    }
    auto value = destination.accept(expression(source, literal));
    if (value) {
        deliver_result(std::move(*value), result, destination);
    }
}

auto BodyRealizer::statement(
    const ConstructionStatement& source,
    ConstructionRegionID owner
) noexcept -> Lowered<LoweringCompleted> {
    auto destination = LoweringStmtBuilder();
    std::visit(
        Overloaded {
            [&](const ConstructionReturn& value) noexcept {
                if (value.value) {
                    result_expression(*value.value, LoweringReturnResult {}, destination);
                } else {
                    emit_return(std::nullopt, destination);
                }
            },
            [&](const ConstructionLoopTransfer& value) noexcept {
                const auto& loop = loops.at(value.loop);
                if (!value.continue_loop) {
                    destination.terminate(
                        generated_statement(TargetBreakStmt {}),
                        loop.break_target
                    );
                } else if (loop.step) {
                    destination.terminate(
                        generated_statement(
                            TargetGotoStmt {
                                .label = *loop.step,
                                .role = TargetJumpRole::ForLoopContinue
                            }
                        ),
                        loop.target
                    );
                } else {
                    destination.terminate(generated_statement(TargetContinueStmt {}), loop.target);
                }
            },
            [&](const ConstructionRethrow& value) noexcept {
                transfer_failure(
                    handlers.at(value.source.handler).storage,
                    value.source.failures,
                    value.destination,
                    destination
                );
            },
            [&](const ConstructionThrow& value) noexcept {
                auto failure = read_value(expression(value.value), destination);
                if (failure) {
                    emit_failure(std::move(*failure), value.destination, destination);
                }
            },
            [&](const ConstructionDiscard& value) noexcept {
                auto evaluation = LoweringStmtBuilder();
                result_expression(value.expression, LoweringDiscardResult {}, evaluation);
                destination.scope(std::move(evaluation));
            },
            [&](const ConstructionInitialize& value) noexcept {
                initialize_binding(value, destination);
            },
            [&](const ConstructionAssign& value) noexcept { assign(value, destination); },
            [&](const ConstructionScope& value) noexcept {
                destination.scope(region(value.region, LoweringDiscardResult {}));
            },
            [&](const ConstructionLoop& value) noexcept { lower_loop(owner, value, destination); },
            [&](const ConstructionRangeLoop& value) noexcept {
                lower_range(owner, value, destination);
            },
            [&](const ConstructionTestReport& value) noexcept {
                auto report = LoweringStmtBuilder();
                lower_report(value, source.origin, report);
                destination.scope(std::move(report));
            }
        },
        source.value
    );
    destination.attribute(
        TargetSourceExpansionAttribution {
            .origin = target_source_origin(context.semantic().provenance(), source.origin)
        }
    );
    const auto normal =
        destination.continues() ? std::optional(LoweringCompleted {}) : std::nullopt;
    return std::move(destination).complete<LoweringCompleted>(normal);
}

auto BodyRealizer::lower_report(
    const ConstructionTestReport& value,
    ProgramOriginID origin,
    LoweringStmtBuilder& destination
) noexcept -> void {
    uses_test_context = true;
    auto should_report = bool_expression(true);
    if (value.condition) {
        auto condition = destination.accept(
            operand({.expression = *value.condition, .use = ConstructionUse::ScalarValue})
        );
        if (!condition) {
            return;
        }
        const auto observed = names.fresh(TargetTemporaryNameKind::Logic);
        destination.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = observed,
                .type = context.intrinsic_type(TargetSymbol::Bool),
                .initializer = std::move(*condition)
            }
        ));
        should_report =
            prefix_expression(TargetPrefixOperator::LogicalNot, name_expression(observed));
    }
    auto report = LoweringStmtBuilder();
    const auto source = target_source_origin(context.semantic().provenance(), origin);
    auto arguments = std::vector<TargetExpr>();
    arguments.push_back(string_expression(source.display_origin, TargetStringLiteralKind::String));
    arguments.push_back(integer_expression(source.line));
    const auto* operation = value.kind == TestReportKind::Check ? "check"
        : value.kind == TestReportKind::Require                 ? "require"
                                                                : "fail";
    arguments.push_back(string_expression(operation, TargetStringLiteralKind::String));
    arguments.push_back(
        value.condition_source.has_value()
            ? string_expression(
                  std::string(context.semantic().provenance().spelling(*value.condition_source)),
                  TargetStringLiteralKind::String
              )
            : intrinsic_expression(TargetSymbol::StdNullopt)
    );
    if (value.message) {
        auto message = destination.accept(
            operand({.expression = *value.message, .use = ConstructionUse::ReadBorrow})
        );
        if (!message) {
            return;
        }
        // Reporting is conditional; evaluating its source operands is eager.
        // Commit the residual message before entering the failure-only branch.
        const auto observed = names.fresh(TargetTemporaryNameKind::Operand);
        destination.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = observed,
                .type = context.lower_type(construction.expression(*value.message).type),
                .initializer = std::move(*message)
            }
        ));
        arguments.push_back(name_expression(observed));
    } else {
        arguments.push_back(intrinsic_expression(TargetSymbol::StdNullopt));
    }
    if (!destination.continues()) {
        return;
    }
    report.emit(statement_expression(call_member(
        name_expression(TargetNameAllocator::test_context()),
        "report_failure",
        std::move(arguments)
    )));
    if (value.kind != TestReportKind::Check) {
        report.terminate(
            generated_statement(TargetReturnStmt {.expression = std::nullopt}),
            LoweringExitTarget {LoweringExitKind::Test, 0}
        );
    }
    if (!value.condition) {
        destination.append(std::move(report));
        return;
    }
    destination.record_exits(report.exits());
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = std::move(should_report), .body = std::move(report).finish()});
    destination.emit(source_statement(
        context.semantic(),
        origin,
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}
