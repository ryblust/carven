module carven:backend.lowering.stmt.lower.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.raw;
import :backend.target.stmt;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto classify_for_initializer_value(TargetStmtValue& statement) noexcept
    -> std::optional<TargetForInitializerValue> {
    if (auto* value = std::get_if<TargetExprStmt>(&statement)) {
        return TargetForInitializerValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetDiscardStmt>(&statement)) {
        return TargetForInitializerValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetVariableStmt>(&statement)) {
        return TargetForInitializerValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetAssignmentStmt>(&statement)) {
        return TargetForInitializerValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetUpdateStmt>(&statement)) {
        return TargetForInitializerValue {std::move(*value)};
    }
    return std::nullopt;
}

auto classify_for_step_value(TargetStmtValue& statement) noexcept
    -> std::optional<TargetForStepValue> {
    if (auto* value = std::get_if<TargetExprStmt>(&statement)) {
        return TargetForStepValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetDiscardStmt>(&statement)) {
        return TargetForStepValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetAssignmentStmt>(&statement)) {
        return TargetForStepValue {std::move(*value)};
    }
    if (auto* value = std::get_if<TargetUpdateStmt>(&statement)) {
        return TargetForStepValue {std::move(*value)};
    }
    return std::nullopt;
}

auto statement_value(TargetForInitializerValue value) noexcept -> TargetStmtValue {
    return std::visit(
        Overloaded {
            [](TargetExprStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetDiscardStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetVariableStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetAssignmentStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetUpdateStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
        },
        std::move(value)
    );
}

auto statement_value(TargetForStepValue value) noexcept -> TargetStmtValue {
    return std::visit(
        Overloaded {
            [](TargetExprStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetDiscardStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetAssignmentStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
            [](TargetUpdateStmt&& source) static noexcept -> TargetStmtValue {
                return std::move(source);
            },
        },
        std::move(value)
    );
}

auto statement_from(TargetForInitializer initializer, TargetAttribution attribution) noexcept
    -> TargetStmt {
    return {
        .value = statement_value(std::move(initializer.value)),
        .attribution = std::move(attribution),
    };
}

auto statement_from(TargetForStep step, TargetAttribution attribution) noexcept -> TargetStmt {
    return {
        .value = statement_value(std::move(step.value)),
        .attribution = std::move(attribution),
    };
}

} // namespace

PreparedTargetStatement::PreparedTargetStatement(TargetStmt statement) noexcept
    : state(std::move(statement)) {}

PreparedTargetStatement::PreparedTargetStatement(State state) noexcept
    : state(std::move(state)) {}

PreparedTargetStatement::PreparedTargetStatement(PreparedTargetStatement&& other) noexcept
    : state(other.take_state()) {}

PreparedTargetStatement::~PreparedTargetStatement() noexcept {
    if (state.has_value()) {
        invariant_violation("prepared target statement was destroyed before consumption");
    }
}

auto PreparedTargetStatement::take_state() noexcept -> State {
    if (!state.has_value()) {
        invariant_violation("prepared target statement was consumed more than once");
    }
    auto result = std::move(*state);
    state.reset();
    return result;
}

auto PreparedTargetStatement::classify_for_initializer() && noexcept -> PreparedTargetStatement {
    auto current = take_state();
    if (!std::holds_alternative<TargetStmt>(current)) {
        invariant_violation("prepared target statement was classified more than once");
    }
    auto statement = std::get<TargetStmt>(std::move(current));
    auto value = classify_for_initializer_value(statement.value);
    if (!value.has_value()) {
        return PreparedTargetStatement(
            State {ClassifiedFallback {
                .statement = std::move(statement),
            }}
        );
    }
    return PreparedTargetStatement(
        State {ClassifiedForInitializer {
            .initializer = TargetForInitializer {.value = std::move(*value)},
            .attribution = std::move(statement.attribution),
        }}
    );
}

auto PreparedTargetStatement::classify_for_step() && noexcept -> PreparedTargetStatement {
    auto current = take_state();
    if (!std::holds_alternative<TargetStmt>(current)) {
        invariant_violation("prepared target statement was classified more than once");
    }
    auto statement = std::get<TargetStmt>(std::move(current));
    auto value = classify_for_step_value(statement.value);
    if (!value.has_value()) {
        return PreparedTargetStatement(
            State {ClassifiedFallback {
                .statement = std::move(statement),
            }}
        );
    }
    return PreparedTargetStatement(
        State {ClassifiedForStep {
            .step = TargetForStep {.value = std::move(*value)},
            .attribution = std::move(statement.attribution),
        }}
    );
}

auto PreparedTargetStatement::is_for_initializer() const noexcept -> bool {
    if (!state.has_value()) {
        invariant_violation("consumed target statement was queried");
    }
    return std::holds_alternative<ClassifiedForInitializer>(*state);
}

auto PreparedTargetStatement::is_for_step() const noexcept -> bool {
    if (!state.has_value()) {
        invariant_violation("consumed target statement was queried");
    }
    return std::holds_alternative<ClassifiedForStep>(*state);
}

auto PreparedTargetStatement::take_for_initializer() && noexcept -> TargetForInitializer {
    auto current = take_state();
    if (!std::holds_alternative<ClassifiedForInitializer>(current)) {
        invariant_violation("prepared target statement is not a for initializer");
    }
    return std::move(std::get<ClassifiedForInitializer>(current).initializer);
}

auto PreparedTargetStatement::take_for_step() && noexcept -> TargetForStep {
    auto current = take_state();
    if (!std::holds_alternative<ClassifiedForStep>(current)) {
        invariant_violation("prepared target statement is not a for step");
    }
    return std::move(std::get<ClassifiedForStep>(current).step);
}

auto PreparedTargetStatement::publish(TargetCallableLowerer& context) && noexcept -> TargetStmtID {
    auto current = take_state();
    auto statement = std::visit(
        Overloaded {
            [](TargetStmt&& value) static noexcept -> TargetStmt { return std::move(value); },
            [](ClassifiedFallback&& value) static noexcept -> TargetStmt {
                return std::move(value.statement);
            },
            [](ClassifiedForInitializer&& value) static noexcept -> TargetStmt {
                return statement_from(std::move(value.initializer), std::move(value.attribution));
            },
            [](ClassifiedForStep&& value) static noexcept -> TargetStmt {
                return statement_from(std::move(value.step), std::move(value.attribution));
            },
        },
        std::move(current)
    );
    return context.target().append_statement(std::move(statement));
}

auto prepare_target_statement(
    TargetCallableLowerer& context,
    HIRStmtID statement_id,
    const TargetControlDestinations& control
) noexcept -> PreparedTargetStatement {
    const auto& source = context.source().statement(statement_id);
    auto value = lower_statement_value(context, source, control);
    return PreparedTargetStatement(
        TargetStmt {
            .value = std::move(value),
            .attribution = {
                .kind = std::holds_alternative<HIRCppStmt>(source.value)
                    ? TargetAttributionKind::RawSource
                    : TargetAttributionKind::SourceOwned,
                .origin = target_source_origin(context, source.origin),
                .reason = std::nullopt,
            },
        }
    );
}

auto lower_statement(
    TargetCallableLowerer& context,
    HIRStmtID statement_id,
    const TargetControlDestinations& control
) noexcept -> TargetStmtID {
    return prepare_target_statement(context, statement_id, control).publish(context);
}

auto with_prelude(
    TargetCallableLowerer& context,
    std::vector<TargetStmtID> prelude,
    TargetStmtValue value
) noexcept -> TargetStmtValue {
    if (prelude.empty()) {
        return value;
    }
    prelude.push_back(context.target().append_lowering_statement(std::move(value)));
    return TargetBlockStmt {
        .statements = std::move(prelude),
        .scoped = false,
    };
}

auto lower_statement_value(
    TargetCallableLowerer& context,
    const HIRStmt& source,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return std::visit(
        [&](const auto& value) noexcept -> TargetStmtValue {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, HIRCppStmt>) {
                return lower_statement(context, value);
            } else if constexpr (std::same_as<Value, HIRTestCheckStmt>
                                 || std::same_as<Value, HIRTestRequireStmt>) {
                return lower_statement(
                    context,
                    value,
                    context.source().expression(value.condition).origin,
                    control
                );
            } else if constexpr (std::same_as<Value, HIRTestFailStmt>) {
                return lower_statement(context, value, source.origin, control);
            } else {
                return lower_statement(context, value, control);
            }
        },
        source.value
    );
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRIfStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto alternate = statement.else_branch.has_value()
        ? lower_block(context, *statement.else_branch, control)
        : std::vector<TargetStmtID>();
    for (auto index = statement.branches.size(); index > 0; --index) {
        const auto& branch = statement.branches[index - 1];
        auto condition = lower_expression(context, branch.condition, control);
        auto branch_body = lower_block(context, branch.body, control);
        auto else_body = alternate.empty()
            ? std::optional<std::vector<TargetStmtID>>()
            : std::optional<std::vector<TargetStmtID>> {std::move(alternate)};
        const auto branch_statement = context.target().append_lowering_statement(
            TargetIfStmt {
                .branches =
                    {
                        TargetIfBranch {
                            .condition = condition.expression,
                            .body = std::move(branch_body),
                        },
                    },
                .else_body = std::move(else_body),
            }
        );
        condition.prelude.push_back(branch_statement);
        alternate = std::move(condition.prelude);
    }
    return TargetBlockStmt {
        .statements = std::move(alternate),
        .scoped = false,
    };
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRMatchStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return TargetBlockStmt {
        .statements = lower_statement_match(context, statement, control),
        .scoped = true,
    };
}

auto lower_void_try(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto& facts = *context.source().try_facts(id);
    const auto protected_failure_set =
        context.source().block_control(expression.body).outward_failure_set;
    if (context.failure_profile(protected_failure_set).ordered_members.empty()) {
        return TargetBlockStmt {
            .statements = lower_block(context, expression.body, control),
            .scoped = true,
        };
    }
    const auto outcome_name = context.fresh_name(TargetTemporaryNameKind::Try);
    const auto transfer_label = context.fresh_name(TargetTemporaryNameKind::Region);
    const auto protected_carrier =
        make_failure_carrier(context, context.source().expression(id).type, protected_failure_set);
    const auto initial_outcome = outcome_success_expression(context, protected_carrier.type);
    auto statements = std::vector<TargetStmtID> {
        context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .name = outcome_name,
                .type = intrinsic_type(context, TargetSymbol::Auto),
                .initializer = initial_outcome,
                .maybe_unused = false,
            }
        ),
    };
    const auto protected_control = control.with_failure(
        FailureContinuation {
            .carrier = protected_carrier,
            .local_transfer = LocalFailureTransfer {
                .destination_name = outcome_name,
                .transfer_label = transfer_label,
            },
        }
    );
    const auto protected_body = lower_block(context, expression.body, protected_control);
    const auto protected_block = context.target().append_lowering_statement(
        TargetBlockStmt {.statements = protected_body, .scoped = true}
    );
    const auto transfer_target = context.target().append_lowering_statement(
        TargetLabelStmt {
            .label = transfer_label,
            .kind = TargetSyntheticControlKind::StatementTryFailureForward,
        }
    );
    const auto protected_region = context.target().append_lowering_statement(
        TargetBlockStmt {
            .statements = {protected_block, transfer_target},
            .scoped = false,
        }
    );
    statements.push_back(protected_region);
    const auto failed = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = member_call_expression(
                context,
                name_expression(context, TargetName {outcome_name}),
                TargetNameAllocator::fixed("has_value")
            ),
        },
    });
    auto handlers = lower_catch_handlers(
        context,
        expression.arms,
        facts.arms,
        protected_carrier,
        outcome_name,
        control
    );
    auto catch_body = complete_catch_handlers(
        context,
        std::move(handlers),
        lower_unhandled_failure_transfer(
            context,
            outcome_name,
            facts.unhandled_failure_set,
            control
        )
    );
    const auto failed_handler = context.target().append_lowering_statement(
        TargetIfStmt {
            .branches =
                {
                    TargetIfBranch {
                        .condition = failed,
                        .body = std::move(catch_body),
                    },
                },
            .else_body = std::nullopt,
        }
    );
    statements.push_back(failed_handler);
    return TargetBlockStmt {
        .statements = std::move(statements),
        .scoped = true,
    };
}

namespace {

auto test_origin_arguments(TargetCallableLowerer& context, ProgramOriginID origin) noexcept
    -> std::vector<TargetExprID> {
    const auto& semantic_origin = context.source().provenance().origin(origin);
    const auto& source = context.source().provenance().source_snapshot(semantic_origin.source_id);
    const auto location = context.source().provenance().location(origin);
    return {
        context.target().append_expression({
            .value =
                TargetLiteralExpr {
                    .value =
                        TargetStringLiteral {
                            .bytes = std::string(source.display_origin()),
                            .kind = TargetStringLiteralKind::String,
                        },
                },
        }),
        context.target().append_expression({
            .value = TargetLiteralExpr {
                .value = TargetIntegerLiteral {
                    .negative = false,
                    .magnitude = location.line,
                    .suffix = TargetIntegerSuffix::None,
                },
            },
        }),
    };
}

auto lower_test_message(
    TargetCallableLowerer& context,
    std::optional<HIRExprID> message,
    const TargetControlDestinations& control,
    std::vector<TargetStmtID>& statements
) noexcept -> std::optional<TargetExprID> {
    if (!message.has_value()) {
        return std::nullopt;
    }
    auto lowered = lower_expression(context, *message, control);
    statements.insert(
        statements.end(),
        std::make_move_iterator(lowered.prelude.begin()),
        std::make_move_iterator(lowered.prelude.end())
    );
    const auto name = context.fresh_name(TargetTemporaryNameKind::TestValue);
    const auto value = name_expression(context, TargetName {name});
    statements.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .name = name,
            .type = intrinsic_type(context, TargetSymbol::Auto),
            .initializer = lowered.expression,
            .maybe_unused = false,
        }
    ));
    return value;
}

template<typename Operation>
auto lower_condition_test(
    TargetCallableLowerer& context,
    const Operation& operation,
    ProgramOriginID origin,
    std::string_view operation_name,
    bool exits_on_failure,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto condition = lower_expression(context, operation.condition, control);
    auto statements = std::move(condition.prelude);
    const auto condition_name = context.fresh_name(TargetTemporaryNameKind::TestValue);
    const auto condition_value = name_expression(context, TargetName {condition_name});
    statements.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .name = condition_name,
            .type = intrinsic_type(context, TargetSymbol::Auto),
            .initializer = condition.expression,
            .maybe_unused = false,
        }
    ));
    const auto message = lower_test_message(context, operation.message, control, statements);
    auto arguments = std::vector<TargetExprID>();
    auto metadata = test_origin_arguments(context, origin);
    arguments.insert(arguments.end(), metadata.begin(), metadata.end());
    arguments.push_back(context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::string(operation_name),
                .kind = TargetStringLiteralKind::StringView,
            },
        },
    }));
    arguments.push_back(context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes =
                    std::string(context.source().provenance().spelling(operation.condition_source)),
                .kind = TargetStringLiteralKind::StringView,
            },
        },
    }));
    arguments.push_back(
        message.has_value() ? *message : name_expression(context, TargetSymbol::StdNullopt)
    );
    const auto report = call_expression(
        context,
        name_expression(context, TargetSymbol::TestingReportFailure),
        std::move(arguments)
    );
    auto failure_body = std::vector<TargetStmtID> {
        context.target().append_lowering_statement(TargetExprStmt {.expression = report}),
    };
    if (exits_on_failure) {
        failure_body.push_back(test_exit_statement(context, control));
    }
    const auto failed = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = condition_value,
        },
    });
    statements.push_back(context.target().append_lowering_statement(
        TargetIfStmt {
            .branches =
                {
                    TargetIfBranch {
                        .condition = failed,
                        .body = std::move(failure_body),
                    },
                },
            .else_body = std::nullopt,
        }
    ));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = true};
}

} // namespace

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestCheckStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return lower_condition_test(context, statement, origin, "check", false, control);
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestRequireStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return lower_condition_test(context, statement, origin, "require", true, control);
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestFailStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto statements = std::vector<TargetStmtID>();
    const auto message = lower_test_message(context, statement.message, control, statements);
    auto arguments = std::vector<TargetExprID>();
    auto metadata = test_origin_arguments(context, origin);
    arguments.insert(arguments.end(), metadata.begin(), metadata.end());
    arguments.push_back(context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = "fail",
                .kind = TargetStringLiteralKind::StringView,
            },
        },
    }));
    arguments.push_back(name_expression(context, TargetSymbol::StdNullopt));
    arguments.push_back(
        message.has_value() ? *message : name_expression(context, TargetSymbol::StdNullopt)
    );
    const auto report = call_expression(
        context,
        name_expression(context, TargetSymbol::TestingReportFailure),
        std::move(arguments)
    );
    statements.push_back(
        context.target().append_lowering_statement(TargetExprStmt {.expression = report})
    );
    statements.push_back(test_exit_statement(context, control));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = true};
}

auto lower_statement(const TargetCallableLowerer& context, const HIRCppStmt& statement) noexcept
    -> TargetStmtValue {
    return TargetRawFragment {
        .bytes = std::string(context.source().provenance().spelling(statement.bytes)),
    };
}
