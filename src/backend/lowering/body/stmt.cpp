module carven:backend.lowering.body.stmt.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyLowerer::emit_return(
    std::optional<TargetExpr> value,
    LoweringStmtBuilder& destination,
    const LoweringResultDestination& result
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (std::holds_alternative<LoweringReturnResult>(result)) {
        if (const auto* callable = std::get_if<TargetCallableBodyExit>(&inputs.exit)) {
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

auto BodyLowerer::emit_failure(TargetExpr value, LoweringStmtBuilder& destination) noexcept
    -> void {
    if (!destination.continues()) {
        return;
    }
    if (failure_destination.has_value()) {
        destination.emit(statement_expression(call_member(
            name_expression(failure_destination->storage),
            "emplace",
            target_expressions(std::move(value))
        )));
        destination.terminate(
            generated_statement(
                TargetGotoStmt {
                    .label = failure_destination->label,
                    .role = TargetJumpRole::FailureTransfer
                }
            ),
            failure_destination->target
        );
        return;
    }
    const auto* callable = std::get_if<TargetCallableBodyExit>(&inputs.exit);
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

auto BodyLowerer::transfer_failure(
    const TargetIdentifier& storage,
    FailureSetID failures,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    auto transfers = LoweringStmtBuilder();
    for (const auto type : context.plan().failure_abi().members(failures)) {
        const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
        transfers.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .name = projection,
                .type = context.pointer_type(context.intrinsic_type(TargetSymbol::Auto)),
                .initializer = template_call_expression(
                    intrinsic_expression(TargetSymbol::StdGetIf),
                    {context.lower_type(type)},
                    target_expressions(
                        address_expression(dereference_expression(name_expression(storage)))
                    )
                )
            }
        ));
        auto transfer = LoweringStmtBuilder();
        emit_failure(
            transfer_expression(dereference_expression(name_expression(projection))),
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
    destination.scope(std::move(transfers));
}

auto BodyLowerer::deliver_result(
    LoweringResult value,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (returns_result(result)) {
        emit_return(remaining_expression(std::move(value)), destination, result);
    } else if (const auto* initialize = std::get_if<LoweringInitializeResult>(&result)) {
        initialize_deferred(initialize->storage, require_expression(std::move(value)), destination);
    } else if (const auto* boolean = std::get_if<LoweringBooleanResult>(&result)) {
        destination.emit(generated_statement(
            TargetAssignmentStmt {
                .target = name_expression(boolean->name),
                .op = TargetAssignmentOperator::Assign,
                .value = require_expression(std::move(value), LoweringResultUse::Observe)
            }
        ));
    } else if (auto expression =
                   remaining_expression(std::move(value), LoweringResultUse::Observe)) {
        destination.emit(
            generated_statement(TargetDiscardStmt {.expression = std::move(*expression)})
        );
    }
}

auto BodyLowerer::result_expression(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (std::holds_alternative<SemIf>(source.value)
        || std::holds_alternative<SemMatch>(source.value)
        || std::holds_alternative<SemTry>(source.value)) {
        structured_expression(source, result, destination);
        return;
    }
    if (std::holds_alternative<LoweringDiscardResult>(result)) {
        static_cast<void>(destination.accept(retain_evaluation(source)));
        return;
    }
    auto literal = LoweringLiteralContext::Exact;
    if (returns_result(result)) {
        if (const auto* callable = std::get_if<TargetCallableBodyExit>(&inputs.exit);
            std::holds_alternative<LoweringYieldResult>(result)
            || (callable != nullptr
                && context.plan()
                       .failure_abi()
                       .members(context.semantic()
                                    .callable_signatures()
                                    .signature(callable->signature)
                                    .failures)
                       .empty())) {
            literal = LoweringLiteralContext::TargetTyped;
        }
    }
    auto value = destination.accept(expression(source, literal));
    if (value) {
        deliver_result(std::move(*value), result, destination);
    }
}

auto BodyLowerer::statement(const SemanticStatement& source) noexcept
    -> Lowered<LoweringCompleted> {
    auto destination = LoweringStmtBuilder();
    std::visit(
        Overloaded {
            [&](const SemReturn& value) noexcept {
                if (value.value.has_value()) {
                    result_expression(*value.value, LoweringReturnResult {}, destination);
                } else {
                    emit_return(std::nullopt, destination);
                }
            },
            [&](const SemBreak&) noexcept {
                if (!loop_continuation) {
                    invariant_violation("break has no owning loop");
                }
                destination.terminate(
                    generated_statement(TargetBreakStmt {}),
                    loop_continuation->break_target
                );
            },
            [&](const SemContinue&) noexcept {
                if (!loop_continuation) {
                    invariant_violation("continue has no owning loop");
                }
                if (loop_continuation->step.has_value()) {
                    destination.terminate(
                        generated_statement(
                            TargetGotoStmt {
                                .label = *loop_continuation->step,
                                .role = TargetJumpRole::ForLoopContinue
                            }
                        ),
                        loop_continuation->target
                    );
                } else {
                    destination.terminate(
                        generated_statement(TargetContinueStmt {}),
                        loop_continuation->target
                    );
                }
            },
            [&](const SemRethrow&) noexcept {
                if (!caught_failure.has_value()) {
                    invariant_violation("rethrow has no lexical catch");
                }
                transfer_failure(caught_failure->storage, caught_failure->failures, destination);
            },
            [&](const SemThrow& value) noexcept {
                auto failure = read_value(expression(value.value), destination);
                if (failure) {
                    emit_failure(std::move(*failure), destination);
                }
            },
            [&](const SemExpressionStatement& value) noexcept {
                auto statements = LoweringStmtBuilder();
                result_expression(value.expression, LoweringDiscardResult {}, statements);
                destination.scope(
                    std::move(statements),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
            [&](const SemInitialize& value) noexcept {
                if (facts(value.initializer).may_exit_value_region) {
                    const auto storage = LoweringDeferredStorage {
                        .name = binding_names.at(value.binding),
                        .value_type = context.lower_type(body.binding(value.binding).type)
                    };
                    delayed_bindings.emplace(value.binding, storage);
                    auto initialization = LoweringStmtBuilder();
                    result_expression(
                        value.initializer,
                        LoweringInitializeResult {.storage = storage},
                        initialization
                    );
                    if (initialization.continues()) {
                        declare_deferred(storage, true, destination);
                    }
                    destination.scope(std::move(initialization));
                    return;
                }
                auto initializer_statements = LoweringStmtBuilder();
                auto initializer = read_value(
                    full_expression(value.initializer, LoweringLiteralContext::TargetTyped),
                    initializer_statements
                );
                if (!initializer_statements.continues()) {
                    destination.scope(std::move(initializer_statements));
                    return;
                }
                if (!facts(value.initializer).needs_lifetime_scope) {
                    destination.append(std::move(initializer_statements));
                } else if (!initializer_statements.empty()) {
                    const auto id = value.binding;
                    delayed_bindings.emplace(
                        id,
                        LoweringDeferredStorage {
                            .name = binding_names.at(id),
                            .value_type = context.lower_type(body.binding(id).type)
                        }
                    );
                    declare_deferred(delayed_bindings.at(id), true, destination);
                    initialize_deferred(
                        delayed_bindings.at(id),
                        std::move(*initializer),
                        initializer_statements
                    );
                    destination.scope(
                        std::move(initializer_statements),
                        TargetSourceExpansionAttribution {
                            .origin =
                                target_source_origin(context.semantic().provenance(), source.origin)
                        }
                    );
                    return;
                }
                declare_binding(value.binding, std::move(*initializer), destination);
            },
            [&](const SemAssign& value) noexcept {
                auto statements = LoweringStmtBuilder();
                const auto* binding = std::get_if<SemBinding>(&value.target.value);
                auto target = read_value(expression(value.target), statements);
                auto stable_target = std::optional<TargetIdentifier>();
                if (!statements.continues()) {
                    destination.scope(std::move(statements));
                    return;
                }
                if (binding == nullptr) {
                    stable_target = names.fresh(TargetTemporaryNameKind::Owner);
                    statements.emit(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::RvalueReference,
                            .maybe_unused = false,
                            .name = *stable_target,
                            .type = context.intrinsic_type(TargetSymbol::Auto),
                            .initializer = std::move(*target)
                        }
                    ));
                }
                const auto target_again = [&]() noexcept {
                    return stable_target.has_value() ? name_expression(*stable_target)
                                                     : binding_expression(binding->binding);
                };
                const auto external =
                    std::holds_alternative<CppTypeValue>(
                        context.semantic().types().type(value.target.type.resolved()).value
                    )
                    || std::holds_alternative<CppTypeValue>(
                        context.semantic().types().type(value.value.type.resolved()).value
                    );
                auto previous = std::optional<TargetIdentifier>();
                if (value.compound.has_value() && !external) {
                    const auto immediate = !facts(value.value).requires_execution;
                    if (!immediate) {
                        const auto name = names.fresh(TargetTemporaryNameKind::Operand);
                        statements.emit(generated_statement(
                            TargetVariableStmt {
                                .binding = TargetVariableBinding::MutableValue,
                                .maybe_unused = false,
                                .name = name,
                                .type = context.lower_type(value.target.type.resolved()),
                                .initializer = target_again()
                            }
                        ));
                        previous = name;
                    }
                }
                auto assignment = TargetAssignmentOperator::Assign;
                if (external && value.compound.has_value()) {
                    switch (*value.compound) {
                        case BinaryOperator::Add: assignment = TargetAssignmentOperator::Add; break;
                        case BinaryOperator::Subtract:
                            assignment = TargetAssignmentOperator::Subtract;
                            break;
                        case BinaryOperator::Multiply:
                            assignment = TargetAssignmentOperator::Multiply;
                            break;
                        case BinaryOperator::Divide:
                            assignment = TargetAssignmentOperator::Divide;
                            break;
                        case BinaryOperator::Remainder:
                            assignment = TargetAssignmentOperator::Remainder;
                            break;
                        case BinaryOperator::BitwiseAnd:
                            assignment = TargetAssignmentOperator::BitwiseAnd;
                            break;
                        case BinaryOperator::BitwiseOr:
                            assignment = TargetAssignmentOperator::BitwiseOr;
                            break;
                        case BinaryOperator::BitwiseXor:
                            assignment = TargetAssignmentOperator::BitwiseXor;
                            break;
                        case BinaryOperator::LeftShift:
                            assignment = TargetAssignmentOperator::LeftShift;
                            break;
                        case BinaryOperator::RightShift:
                            assignment = TargetAssignmentOperator::RightShift;
                            break;
                        default: invariant_violation("invalid external compound assignment");
                    }
                }
                auto assigned = read_value(expression(value.value), statements);
                if (assigned) {
                    if (value.compound.has_value() && !external) {
                        assigned = binary(
                            previous ? name_expression(*previous) : target_again(),
                            *value.compound,
                            std::move(*assigned),
                            value.target.type.resolved()
                        );
                    }
                    statements.emit(generated_statement(
                        TargetAssignmentStmt {
                            .target = target_again(),
                            .op = assignment,
                            .value = std::move(*assigned)
                        }
                    ));
                }
                destination.scope(
                    std::move(statements),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
            [&](const SemLoop& value) noexcept { lower_loop(value, destination); },
            [&](const SemRangeLoop& value) noexcept { lower_range(value, destination); },
            [&](const SemTestReport& value) noexcept {
                if (!destination.continues()) {
                    return;
                }
                auto report = LoweringStmtBuilder();
                lower_report(value, source.origin, report);
                destination.scope(
                    std::move(report),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
            [&](const OwnedSemanticRegion& value) noexcept {
                destination.scope(
                    region(*value, LoweringDiscardResult {}),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
        },
        source.value
    );
    destination.attribute(
        TargetSourceExpansionAttribution {
            .origin = target_source_origin(context.semantic().provenance(), source.origin)
        }
    );
    return std::move(destination)
        .complete<LoweringCompleted>(
            destination.continues() ? std::optional(LoweringCompleted {}) : std::nullopt
        );
}

auto BodyLowerer::lower_report(
    const SemTestReport& value,
    ProgramOriginID origin,
    LoweringStmtBuilder& destination
) noexcept -> void {
    uses_test_context = true;
    auto should_report = bool_expression(true);
    if (value.condition) {
        auto condition = destination.accept(operand(*value.condition, OperandUse::Snapshot));
        if (!condition) {
            return;
        }
        should_report = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(*condition));
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
        auto message = destination.accept(operand(*value.message, OperandUse::Snapshot));
        if (!message) {
            return;
        }
        arguments.push_back(std::move(*message));
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
