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

namespace body_lowering {
auto BodyLowerer::emit_return(
    std::optional<TargetExpr> value,
    StatementSequence& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (region_return.has_value()) {
        *region_return = true;
    } else {
        if (const auto* callable = std::get_if<TargetCallableBodyExit>(&inputs.exit)) {
            const auto& signature =
                context.semantic().callable_signatures().signature(callable->signature);
            if (!context.plan().failure_abi().members(signature.failures).empty()) {
                auto arguments = std::vector<TargetExpr>();
                if (value.has_value()) {
                    arguments.push_back(std::move(*value));
                }
                value = call_expression(
                    static_member_expression(
                        context.outcome_type(callable->signature),
                        TargetIdentifier::from_spelling("success")
                    ),
                    std::move(arguments)
                );
            }
        }
    }
    destination.terminate(generated_statement(TargetReturnStmt {.expression = std::move(value)}));
}

auto BodyLowerer::emit_failure(TargetExpr value, StatementSequence& destination) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (failure_destination.has_value()) {
        failure_destination->used = true;
        destination.emit(statement_expression(call_member(
            name_expression(failure_destination->storage),
            "emplace",
            target_expressions(std::move(value))
        )));
        destination.terminate(generated_statement(
            TargetGotoStmt {
                .label = failure_destination->label,
                .role = TargetJumpRole::FailureTransfer
            }
        ));
        return;
    }
    const auto* callable = std::get_if<TargetCallableBodyExit>(&inputs.exit);
    if (callable == nullptr) {
        invariant_violation("failure escaped a test body");
    }
    destination.terminate(generated_statement(
        TargetReturnStmt {
            .expression = call_expression(
                static_member_expression(
                    context.outcome_type(callable->signature),
                    TargetIdentifier::from_spelling("failure")
                ),
                target_expressions(std::move(value))
            )
        }
    ));
}

auto BodyLowerer::transfer_failure(
    const TargetIdentifier& storage,
    FailureSetID failures,
    StatementSequence& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    auto transfers = StatementSequence();
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
        auto transfer = StatementSequence();
        emit_failure(
            transfer_expression(dereference_expression(name_expression(projection))),
            transfer
        );
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = name_expression(projection), .body = std::move(transfer).finish()}
        );
        transfers.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    transfers.terminate(
        generated_statement(TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof})
    );
    destination.block(std::move(transfers));
}

auto BodyLowerer::result_expression(
    const SemanticExpression& source,
    ResultDestination result,
    StatementSequence& destination
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
    auto value = expression(source, destination);
    if (!destination.continues()) {
        return;
    }
    if (result.use == ResultUse::Return) {
        if (context.is_void(source.type.resolved())) {
            destination.emit(statement_expression(std::move(*value)));
            emit_return(std::nullopt, destination);
        } else {
            emit_return(std::move(*value), destination);
        }
    } else if (result.use == ResultUse::Store) {
        destination.emit(statement_expression(call_member(
            name_expression(*result.storage),
            "emplace",
            target_expressions(std::move(*value))
        )));
    } else {
        destination.emit(source_statement(
            context.semantic(),
            source.origin,
            TargetDiscardStmt {.expression = std::move(*value)}
        ));
    }
}

auto BodyLowerer::statement(
    const SemanticStatement& source,
    StatementSequence& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    std::visit(
        Overloaded {
            [&](const SemReturn& value) noexcept {
                if (value.value.has_value()) {
                    result_expression(
                        *value.value,
                        {.use = ResultUse::Return, .storage = std::nullopt},
                        destination
                    );
                } else {
                    emit_return(std::nullopt, destination);
                }
            },
            [&](const SemBreak&) noexcept {
                loop_continuation.breaks = true;
                destination.terminate(generated_statement(TargetBreakStmt {}));
            },
            [&](const SemContinue&) noexcept {
                loop_continuation.used = true;
                if (loop_continuation.step.has_value()) {
                    destination.terminate(generated_statement(
                        TargetGotoStmt {
                            .label = *loop_continuation.step,
                            .role = TargetJumpRole::ForLoopContinue
                        }
                    ));
                } else {
                    destination.terminate(generated_statement(TargetContinueStmt {}));
                }
            },
            [&](const SemRethrow&) noexcept {
                if (!caught_failure.has_value()) {
                    invariant_violation("rethrow has no lexical catch");
                }
                transfer_failure(caught_failure->storage, caught_failure->failures, destination);
            },
            [&](const SemThrow& value) noexcept {
                auto failure = expression(value.value, destination);
                if (failure) {
                    emit_failure(std::move(*failure), destination);
                }
            },
            [&](const SemExpressionStatement& value) noexcept {
                auto statements = StatementSequence();
                result_expression(
                    value.expression,
                    {.use = ResultUse::Discard, .storage = std::nullopt},
                    statements
                );
                destination.block(
                    std::move(statements),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
            [&](const SemInitialize& value) noexcept {
                auto initializer_statements = StatementSequence();
                auto initializer = expression(value.initializer, initializer_statements);
                if (!initializer_statements.continues()) {
                    destination.block(std::move(initializer_statements));
                    return;
                }
                if (!initializer_statements.empty()
                    && context.plan()
                           .failure_abi()
                           .members(value.initializer.failures.resolved())
                           .empty()
                    && !value.initializer.exits_test) {
                    initializer_statements.terminate(generated_statement(
                        TargetReturnStmt {.expression = std::move(*initializer)}
                    ));

                    initializer = TargetExpr {
                        .value = TargetRegionExpr {
                            .result = context.lower_type(value.initializer.type.resolved()),
                            .body = std::move(initializer_statements).finish()
                        }
                    };
                } else if (!initializer_statements.empty()) {
                    const auto id = value.binding;
                    delayed_bindings.insert(id);
                    destination.emit(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = true,
                            .name = binding_names.at(id),
                            .type =
                                context.optional_type(context.lower_type(body.binding(id).type)),
                            .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
                        }
                    ));
                    initializer_statements.emit(statement_expression(call_member(
                        name_expression(binding_names.at(id)),
                        "emplace",
                        target_expressions(std::move(*initializer))
                    )));
                    destination.block(
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
                auto statements = StatementSequence();
                const auto* binding = std::get_if<SemBinding>(&value.target.value);
                auto target = expression(value.target, statements);
                auto stable_target = std::optional<TargetIdentifier>();
                if (!statements.continues()) {
                    destination.block(std::move(statements));
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
                    target = name_expression(*stable_target);
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
                auto previous = std::optional<TargetExpr>();
                if (value.compound.has_value() && !external) {
                    const auto immediate = std::holds_alternative<SemConstant>(value.value.value)
                        || std::holds_alternative<SemBinding>(value.value.value);
                    if (immediate) {
                        previous = target_again();
                    } else {
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
                        previous = name_expression(name);
                    }
                }
                auto assigned = expression(value.value, statements);
                if (!statements.continues()) {
                    destination.block(std::move(statements));
                    return;
                }
                if (value.compound.has_value() && !external) {
                    assigned = binary(
                        std::move(*previous),
                        *value.compound,
                        std::move(*assigned),
                        value.target.type.resolved()
                    );
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
                statements.emit(generated_statement(
                    TargetAssignmentStmt {
                        .target = std::move(*target),
                        .op = assignment,
                        .value = std::move(*assigned)
                    }
                ));
                destination.block(
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
                auto report = StatementSequence();
                lower_report(value, source.origin, report);
                destination.block(
                    std::move(report),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
            [&](const OwnedSemanticRegion& value) noexcept {
                destination.block(
                    region(*value, {.use = ResultUse::Discard, .storage = std::nullopt}),
                    TargetSourceExpansionAttribution {
                        .origin =
                            target_source_origin(context.semantic().provenance(), source.origin)
                    }
                );
            },
        },
        source.value
    );
}

auto BodyLowerer::lower_report(
    const SemTestReport& value,
    ProgramOriginID origin,
    StatementSequence& destination
) noexcept -> void {
    uses_test_context = true;
    auto should_report = bool_expression(true);
    if (value.condition) {
        auto condition = operand(*value.condition, destination, OperandUse::Snapshot);
        if (!condition) {
            return;
        }
        should_report = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(*condition));
    }
    auto report = StatementSequence();
    const auto source = target_source_origin(context.semantic().provenance(), origin);
    auto arguments = std::vector<TargetExpr>();
    arguments.push_back(string_expression(source.display_origin, TargetStringLiteralKind::String));
    arguments.push_back(integer_expression(source.line));
    const auto* const operation = value.kind == TestReportKind::Check ? "check"
        : value.kind == TestReportKind::Require                       ? "require"
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
        auto message = operand(*value.message, destination, OperandUse::Snapshot);
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
        report.terminate(generated_statement(TargetReturnStmt {.expression = std::nullopt}));
    }
    if (!value.condition) {
        destination.append(std::move(report));
        return;
    }
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = std::move(should_report), .body = std::move(report).finish()});
    destination.emit(source_statement(
        context.semantic(),
        origin,
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}

} // namespace body_lowering
