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
namespace {

auto full_expression_block(std::vector<TargetStmt> statements) noexcept -> TargetBlockStmt {
    const auto needs_scope = [&](this const auto& self,
                                 std::span<const TargetStmt> statements) noexcept -> bool {
        return std::ranges::any_of(statements, [&](const TargetStmt& statement) noexcept {
            if (std::holds_alternative<TargetVariableStmt>(statement.value)
                || std::holds_alternative<TargetLabelStmt>(statement.value)) {
                return true;
            }
            const auto* block = std::get_if<TargetBlockStmt>(&statement.value);
            return block != nullptr && !block->scoped && self(block->statements);
        });
    };
    const auto scoped = needs_scope(statements);
    return {.statements = std::move(statements), .scoped = scoped};
}

} // namespace

auto BodyLowerer::emit_return(
    std::optional<TargetExpr> value,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    if (!falls_through(destination)) {
        return;
    }
    if (!returning_region) {
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
    destination.push_back(generated_statement(TargetReturnStmt {.expression = std::move(value)}));
}

auto BodyLowerer::emit_failure(TargetExpr value, std::vector<TargetStmt>& destination) noexcept
    -> void {
    if (!falls_through(destination)) {
        return;
    }
    if (failure_destination.has_value()) {
        destination.push_back(statement_expression(call_member(
            name_expression(failure_destination->storage),
            "emplace",
            target_expressions(std::move(value))
        )));
        destination.push_back(generated_statement(
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
    destination.push_back(generated_statement(
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
    std::vector<TargetStmt>& destination
) noexcept -> void {
    auto transfers = std::vector<TargetStmt>();
    for (const auto type : context.plan().failure_abi().members(failures)) {
        const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
        transfers.push_back(generated_statement(
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
        auto transfer = std::vector<TargetStmt>();
        emit_failure(
            transfer_expression(dereference_expression(name_expression(projection))),
            transfer
        );
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = name_expression(projection), .body = std::move(transfer)});
        transfers.push_back(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    transfers.push_back(
        generated_statement(TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof})
    );
    destination.push_back(generated_statement(full_expression_block(std::move(transfers))));
}

auto BodyLowerer::result_expression(
    const SemIRExpression& source,
    ResultDestination result,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    if (const auto* sequence = std::get_if<SemSequence<TypeID, FailureSetID>>(&source.value)) {
        for (auto index = 0uz; index < sequence->expressions.size(); ++index) {
            const auto& item = sequence->expressions[index];
            if (index + 1uz == sequence->expressions.size() || context.is_void(item.type)) {
                result_expression(
                    item,
                    {.use = ResultUse::Discard, .storage = std::nullopt},
                    destination
                );
            } else {
                auto retained = operand(item, destination, OperandUse::Snapshot);
                destination.push_back(
                    generated_statement(TargetDiscardStmt {.expression = std::move(retained)})
                );
            }
        }
        return;
    }
    if (std::holds_alternative<SemIf<TypeID, FailureSetID>>(source.value)
        || std::holds_alternative<SemMatch<TypeID, FailureSetID>>(source.value)
        || std::holds_alternative<SemTry<TypeID, FailureSetID>>(source.value)) {
        structured_expression(source, result, destination);
        return;
    }
    auto value = expression(source, destination);
    if (!falls_through(destination)) {
        return;
    }
    if (result.use == ResultUse::Return) {
        if (context.is_void(source.type)) {
            destination.push_back(statement_expression(std::move(value)));
            emit_return(std::nullopt, destination);
        } else {
            emit_return(std::move(value), destination);
        }
    } else if (result.use == ResultUse::Store) {
        destination.push_back(statement_expression(call_member(
            name_expression(*result.storage),
            "emplace",
            target_expressions(std::move(value))
        )));
    } else {
        destination.push_back(source_statement(
            context.semantic(),
            source.origin,
            TargetDiscardStmt {.expression = std::move(value)}
        ));
    }
}

auto BodyLowerer::statement(
    const SemIRStatement& source,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    if (!falls_through(destination)) {
        return;
    }
    std::visit(
        Overloaded {
            [&](const SemReturn<TypeID, FailureSetID>& value) noexcept {
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
                destination.push_back(generated_statement(TargetBreakStmt {}));
            },
            [&](const SemContinue&) noexcept {
                continue_label_used = true;
                destination.push_back(generated_statement(TargetContinueStmt {}));
            },
            [&](const SemRethrow&) noexcept {
                if (!caught_failure.has_value()) {
                    invariant_violation("rethrow has no lexical catch");
                }
                transfer_failure(caught_failure->storage, caught_failure->failures, destination);
            },
            [&](const SemThrow<TypeID, FailureSetID>& value) noexcept {
                emit_failure(expression(value.value, destination), destination);
            },
            [&](const SemExpressionStatement<TypeID, FailureSetID>& value) noexcept {
                auto statements = std::vector<TargetStmt>();
                result_expression(
                    value.expression,
                    {.use = ResultUse::Discard, .storage = std::nullopt},
                    statements
                );
                destination.push_back(source_statement(
                    context.semantic(),
                    source.origin,
                    full_expression_block(std::move(statements))
                ));
            },
            [&](const SemInitialize<TypeID, FailureSetID>& value) noexcept {
                auto initializer_statements = std::vector<TargetStmt>();
                auto initializer = expression(value.initializer, initializer_statements);
                if (!falls_through(initializer_statements)) {
                    destination.push_back(generated_statement(
                        full_expression_block(std::move(initializer_statements))
                    ));
                    return;
                }
                if (!initializer_statements.empty()
                    && context.plan().failure_abi().members(value.initializer.failures).empty()
                    && !value.initializer.exits_test) {
                    initializer_statements.push_back(
                        generated_statement(TargetReturnStmt {.expression = std::move(initializer)})
                    );
                    mark_unused(initializer_statements);
                    initializer = TargetExpr {
                        .value = TargetRegionExpr {
                            .result = context.lower_type(value.initializer.type),
                            .body = std::move(initializer_statements)
                        }
                    };
                } else if (!initializer_statements.empty()) {
                    const auto id = value.binding;
                    delayed_bindings.insert(id);
                    destination.push_back(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = false,
                            .name = binding_names.at(id),
                            .type =
                                context.optional_type(context.lower_type(body.binding(id).type)),
                            .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
                        }
                    ));
                    initializer_statements.push_back(statement_expression(call_member(
                        name_expression(binding_names.at(id)),
                        "emplace",
                        target_expressions(std::move(initializer))
                    )));
                    destination.push_back(source_statement(
                        context.semantic(),
                        source.origin,
                        full_expression_block(std::move(initializer_statements))
                    ));
                    return;
                }
                declare_binding(value.binding, std::move(initializer), destination);
            },
            [&](const SemAssign<TypeID, FailureSetID>& value) noexcept {
                auto statements = std::vector<TargetStmt>();
                const auto* binding = std::get_if<SemBinding>(&value.target.value);
                auto target = expression(value.target, statements);
                auto stable_target = std::optional<TargetIdentifier>();
                if (!falls_through(statements)) {
                    destination.push_back(
                        generated_statement(full_expression_block(std::move(statements)))
                    );
                    return;
                }
                if (binding == nullptr) {
                    stable_target = names.fresh(TargetTemporaryNameKind::Owner);
                    statements.push_back(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::RvalueReference,
                            .maybe_unused = false,
                            .name = *stable_target,
                            .type = context.intrinsic_type(TargetSymbol::Auto),
                            .initializer = std::move(target)
                        }
                    ));
                    target = name_expression(*stable_target);
                }
                const auto target_again = [&]() noexcept {
                    return stable_target.has_value() ? name_expression(*stable_target)
                                                     : binding_expression(binding->binding);
                };
                auto previous = std::optional<TargetExpr>();
                if (value.compound.has_value()) {
                    const auto immediate = std::holds_alternative<SemLiteral>(value.value.value)
                        || std::holds_alternative<SemConstant>(value.value.value)
                        || std::holds_alternative<SemBinding>(value.value.value);
                    if (immediate) {
                        previous = target_again();
                    } else {
                        const auto name = names.fresh(TargetTemporaryNameKind::Operand);
                        statements.push_back(generated_statement(
                            TargetVariableStmt {
                                .binding = TargetVariableBinding::MutableValue,
                                .maybe_unused = false,
                                .name = name,
                                .type = context.lower_type(value.target.type),
                                .initializer = target_again()
                            }
                        ));
                        previous = name_expression(name);
                    }
                }
                auto assigned = expression(value.value, statements);
                if (!falls_through(statements)) {
                    destination.push_back(
                        generated_statement(full_expression_block(std::move(statements)))
                    );
                    return;
                }
                if (value.compound.has_value()) {
                    assigned = binary(
                        std::move(*previous),
                        *value.compound,
                        std::move(assigned),
                        value.target.type
                    );
                }
                statements.push_back(generated_statement(
                    TargetAssignmentStmt {
                        .target = std::move(target),
                        .op = TargetAssignmentOperator::Assign,
                        .value = std::move(assigned)
                    }
                ));
                destination.push_back(source_statement(
                    context.semantic(),
                    source.origin,
                    full_expression_block(std::move(statements))
                ));
            },
            [&](const SemLoop<TypeID, FailureSetID>& value) noexcept {
                lower_loop(value, destination);
            },
            [&](const SemRangeLoop<TypeID, FailureSetID>& value) noexcept {
                lower_range(value, destination);
            },
            [&](const SemTestReport<TypeID, FailureSetID>& value) noexcept {
                if (!falls_through(destination)) {
                    return;
                }
                auto report = std::vector<TargetStmt>();
                lower_report(value, source.origin, report);
                destination.push_back(source_statement(
                    context.semantic(),
                    source.origin,
                    full_expression_block(std::move(report))
                ));
            },
            [&](const OwnedSemanticRegion<TypeID, FailureSetID>& value) noexcept {
                destination.push_back(source_statement(
                    context.semantic(),
                    source.origin,
                    TargetBlockStmt {
                        .statements =
                            region(*value, {.use = ResultUse::Discard, .storage = std::nullopt}),
                        .scoped = true
                    }
                ));
            },
        },
        source.value
    );
}

auto BodyLowerer::lower_report(
    const SemTestReport<TypeID, FailureSetID>& value,
    ProgramOriginID origin,
    std::vector<TargetStmt>& destination
) noexcept -> void {
    uses_test_context = true;
    auto should_report = value.condition.has_value()
        ? prefix_expression(
              TargetPrefixOperator::LogicalNot,
              operand(*value.condition, destination, OperandUse::Snapshot)
          )
        : bool_expression(true);
    auto report = std::vector<TargetStmt>();
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
    arguments.push_back(
        value.message.has_value() ? operand(*value.message, destination, OperandUse::Snapshot)
                                  : intrinsic_expression(TargetSymbol::StdNullopt)
    );
    if (!falls_through(destination)) {
        return;
    }
    report.push_back(statement_expression(call_member(
        name_expression(TargetNameAllocator::test_context()),
        "report_failure",
        std::move(arguments)
    )));
    if (value.kind != TestReportKind::Check) {
        report.push_back(generated_statement(TargetReturnStmt {.expression = std::nullopt}));
    }
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back({.condition = std::move(should_report), .body = std::move(report)});
    destination.push_back(source_statement(
        context.semantic(),
        origin,
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}

} // namespace body_lowering
