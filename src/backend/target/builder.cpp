module carven:backend.target.builder.impl;

import :backend.target.builder;
import :support.invariant;
import :support.visit;
import std;

auto TargetUnitBuilder::intern_type(TargetType type) noexcept -> TargetTypeID {
    const auto existing = std::ranges::find(storage.types.values(), type);
    if (existing != storage.types.values().end()) {
        return TargetTypeID::from_index(
            static_cast<std::uint32_t>(std::distance(storage.types.values().begin(), existing))
        );
    }
    return storage.types.add(std::move(type));
}

auto TargetUnitBuilder::append_expression(TargetExpr expression) noexcept -> TargetExprID {
    return storage.expressions.add(std::move(expression));
}

auto TargetUnitBuilder::clone_expression_occurrences(
    std::span<const TargetExprID> expression_ids,
    CloneActivePath& active_path
) noexcept -> std::vector<TargetExprID> {
    auto cloned_expression_ids = std::vector<TargetExprID>();
    cloned_expression_ids.reserve(expression_ids.size());
    for (const auto expression_id : expression_ids) {
        cloned_expression_ids.push_back(clone_expression_occurrence(expression_id, active_path));
    }
    return cloned_expression_ids;
}

auto TargetUnitBuilder::clone_statement_occurrences(
    std::span<const TargetStmtID> statement_ids,
    CloneActivePath& active_path
) noexcept -> std::vector<TargetStmtID> {
    auto cloned_statement_ids = std::vector<TargetStmtID>();
    cloned_statement_ids.reserve(statement_ids.size());
    for (const auto statement_id : statement_ids) {
        cloned_statement_ids.push_back(clone_statement_occurrence(statement_id, active_path));
    }
    return cloned_statement_ids;
}

auto TargetUnitBuilder::clone_expression_occurrence(TargetExprID expression_id) noexcept
    -> TargetExprID {
    auto active_path = CloneActivePath {
        .expressions = std::vector<std::uint8_t>(storage.expressions.size()),
        .statements = std::vector<std::uint8_t>(storage.statements.size()),
    };
    return clone_expression_occurrence(expression_id, active_path);
}

auto TargetUnitBuilder::clone_expression_occurrence(
    TargetExprID expression_id,
    CloneActivePath& active_path
) noexcept -> TargetExprID {
    if (expression_id.index() >= active_path.expressions.size()) {
        invariant_violation("target expression clone source is out of range");
    }
    auto& active = active_path.expressions[expression_id.index()];
    if (active != 0u) {
        invariant_violation("target expression clone source contains a recursive cycle");
    }
    active = 1u;
    const auto source = expression(expression_id).value;
    auto value = std::visit(
        Overloaded {
            [](const TargetNameExpr& expression) static noexcept -> TargetExprValue {
                return expression;
            },
            [](const TargetIntrinsicNameExpr& expression) static noexcept -> TargetExprValue {
                return expression;
            },
            [](const TargetLiteralExpr& expression) static noexcept -> TargetExprValue {
                return expression;
            },
            [&](const TargetPrefixExpr& expression) noexcept -> TargetExprValue {
                return TargetPrefixExpr {
                    .op = expression.op,
                    .operand_id = clone_expression_occurrence(expression.operand_id, active_path),
                };
            },
            [&](const TargetBinaryExpr& expression) noexcept -> TargetExprValue {
                return TargetBinaryExpr {
                    .left = clone_expression_occurrence(expression.left, active_path),
                    .op = expression.op,
                    .right = clone_expression_occurrence(expression.right, active_path),
                };
            },
            [&](const TargetCallExpr& expression) noexcept -> TargetExprValue {
                return TargetCallExpr {
                    .callee = clone_expression_occurrence(expression.callee, active_path),
                    .template_argument_type_ids = expression.template_argument_type_ids,
                    .arguments = clone_expression_occurrences(expression.arguments, active_path),
                };
            },
            [&](const TargetArrayExpr& expression) noexcept -> TargetExprValue {
                return TargetArrayExpr {
                    .element_type_id = expression.element_type_id,
                    .extent = clone_expression_occurrence(expression.extent, active_path),
                    .element_ids =
                        clone_expression_occurrences(expression.element_ids, active_path),
                };
            },
            [&](const TargetConstructionExpr& expression) noexcept -> TargetExprValue {
                auto initializer = std::visit(
                    Overloaded {
                        [](const std::monostate&) static noexcept
                            -> std::variant<
                                std::monostate,
                                std::vector<TargetExprID>,
                                std::vector<TargetFieldInitializer>> { return std::monostate {}; },
                        [&](const std::vector<TargetExprID>& value_ids) noexcept
                            -> std::variant<
                                std::monostate,
                                std::vector<TargetExprID>,
                                std::vector<TargetFieldInitializer>> {
                            return clone_expression_occurrences(value_ids, active_path);
                        },
                        [&](const std::vector<TargetFieldInitializer>& fields) noexcept
                            -> std::variant<
                                std::monostate,
                                std::vector<TargetExprID>,
                                std::vector<TargetFieldInitializer>> {
                            auto result = std::vector<TargetFieldInitializer>();
                            result.reserve(fields.size());
                            for (const auto& field : fields) {
                                result.push_back({
                                    .name = field.name,
                                    .value = clone_expression_occurrence(field.value, active_path),
                                });
                            }
                            return result;
                        },
                    },
                    expression.initializer
                );
                return TargetConstructionExpr {
                    .type = expression.type,
                    .initializer = std::move(initializer),
                };
            },
            [&](const TargetIndexExpr& expression) noexcept -> TargetExprValue {
                return TargetIndexExpr {
                    .operand_id = clone_expression_occurrence(expression.operand_id, active_path),
                    .index = clone_expression_occurrence(expression.index, active_path),
                };
            },
            [&](const TargetMemberExpr& expression) noexcept -> TargetExprValue {
                return TargetMemberExpr {
                    .operand_id = clone_expression_occurrence(expression.operand_id, active_path),
                    .name = expression.name,
                };
            },
            [&](const TargetScopeMemberExpr& expression) noexcept -> TargetExprValue {
                return TargetScopeMemberExpr {
                    .operand_id = clone_expression_occurrence(expression.operand_id, active_path),
                    .name = expression.name,
                };
            },
            [](const TargetStaticMemberExpr& expression) static noexcept -> TargetExprValue {
                return expression;
            },
            [](const TargetForwardExpr& expression) static noexcept -> TargetExprValue {
                return expression;
            },
            [&](const TargetStaticCastExpr& expression) noexcept -> TargetExprValue {
                return TargetStaticCastExpr {
                    .type = expression.type,
                    .operand_id = clone_expression_occurrence(expression.operand_id, active_path),
                };
            },
            [&](const TargetLambdaExpr& expression) noexcept -> TargetExprValue {
                return TargetLambdaExpr {
                    .reason = expression.reason,
                    .body = clone_statement_occurrences(expression.body, active_path),
                };
            },
            [&](const TargetClosureExpr& expression) noexcept -> TargetExprValue {
                return TargetClosureExpr {
                    .captures = expression.captures,
                    .parameters = expression.parameters,
                    .result = expression.result,
                    .body = clone_statement_occurrences(expression.body, active_path),
                };
            },
        },
        source
    );
    active = 0u;
    return append_expression({.value = std::move(value)});
}

auto TargetUnitBuilder::clone_statement_occurrence(
    TargetStmtID statement_id,
    CloneActivePath& active_path
) noexcept -> TargetStmtID {
    if (statement_id.index() >= active_path.statements.size()) {
        invariant_violation("target statement clone source is out of range");
    }
    auto& active = active_path.statements[statement_id.index()];
    if (active != 0u) {
        invariant_violation("target statement clone source contains a recursive cycle");
    }
    active = 1u;
    const auto source = statement(statement_id);
    auto value = std::visit(
        Overloaded {
            [&](const TargetExprStmt& current) noexcept -> TargetStmtValue {
                return TargetExprStmt {
                    .expression = clone_expression_occurrence(current.expression, active_path),
                };
            },
            [&](const TargetDiscardStmt& current) noexcept -> TargetStmtValue {
                return TargetDiscardStmt {
                    .expression = clone_expression_occurrence(current.expression, active_path),
                };
            },
            [&](const TargetReturnStmt& current) noexcept -> TargetStmtValue {
                return TargetReturnStmt {
                    .expression =
                        current.expression.transform([&](TargetExprID expression_id) noexcept {
                            return clone_expression_occurrence(expression_id, active_path);
                        }),
                };
            },
            [&](const TargetVariableStmt& current) noexcept -> TargetStmtValue {
                return TargetVariableStmt {
                    .binding = current.binding,
                    .name = current.name,
                    .type = current.type,
                    .initializer = clone_expression_occurrence(current.initializer, active_path),
                    .maybe_unused = current.maybe_unused,
                };
            },
            [&](const TargetBlockStmt& current) noexcept -> TargetStmtValue {
                return TargetBlockStmt {
                    .statements = clone_statement_occurrences(current.statements, active_path),
                    .scoped = current.scoped,
                };
            },
            [&](const TargetAssignmentStmt& current) noexcept -> TargetStmtValue {
                return TargetAssignmentStmt {
                    .target = clone_expression_occurrence(current.target, active_path),
                    .op = current.op,
                    .value = clone_expression_occurrence(current.value, active_path),
                };
            },
            [&](const TargetUpdateStmt& current) noexcept -> TargetStmtValue {
                return TargetUpdateStmt {
                    .op = current.op,
                    .target = clone_expression_occurrence(current.target, active_path),
                };
            },
            [](const TargetBreakStmt& current) static noexcept -> TargetStmtValue {
                return current;
            },
            [](const TargetContinueStmt& current) static noexcept -> TargetStmtValue {
                return current;
            },
            [](const TargetGotoStmt& current) static noexcept -> TargetStmtValue {
                return current;
            },
            [](const TargetLabelStmt& current) static noexcept -> TargetStmtValue {
                return current;
            },
            [&](const TargetIfStmt& current) noexcept -> TargetStmtValue {
                auto branches = std::vector<TargetIfBranch>();
                branches.reserve(current.branches.size());
                for (const auto& branch : current.branches) {
                    branches.push_back({
                        .condition = clone_expression_occurrence(branch.condition, active_path),
                        .body = clone_statement_occurrences(branch.body, active_path),
                    });
                }
                return TargetIfStmt {
                    .branches = std::move(branches),
                    .else_body = current.else_body.transform(
                        [&](const std::vector<TargetStmtID>& body_ids) noexcept {
                            return clone_statement_occurrences(body_ids, active_path);
                        }
                    ),
                };
            },
            [&](const TargetWhileStmt& current) noexcept -> TargetStmtValue {
                return TargetWhileStmt {
                    .condition = clone_expression_occurrence(current.condition, active_path),
                    .body = clone_statement_occurrences(current.body, active_path),
                };
            },
            [&](const TargetForStmt& current) noexcept -> TargetStmtValue {
                auto initializer = current.initializer.transform([&](
                                                                     const TargetForInitializer&
                                                                         source_initializer
                                                                 ) noexcept {
                    auto initializer_value = std::visit(
                        Overloaded {
                            [&](const TargetExprStmt& source) noexcept
                                -> TargetForInitializerValue {
                                return TargetExprStmt {
                                    .expression =
                                        clone_expression_occurrence(source.expression, active_path),
                                };
                            },
                            [&](const TargetDiscardStmt& source) noexcept
                                -> TargetForInitializerValue {
                                return TargetDiscardStmt {
                                    .expression =
                                        clone_expression_occurrence(source.expression, active_path),
                                };
                            },
                            [&](const TargetVariableStmt& source) noexcept
                                -> TargetForInitializerValue {
                                return TargetVariableStmt {
                                    .binding = source.binding,
                                    .name = source.name,
                                    .type = source.type,
                                    .initializer = clone_expression_occurrence(
                                        source.initializer,
                                        active_path
                                    ),
                                    .maybe_unused = source.maybe_unused,
                                };
                            },
                            [&](const TargetAssignmentStmt& source) noexcept
                                -> TargetForInitializerValue {
                                return TargetAssignmentStmt {
                                    .target =
                                        clone_expression_occurrence(source.target, active_path),
                                    .op = source.op,
                                    .value = clone_expression_occurrence(source.value, active_path),
                                };
                            },
                            [&](const TargetUpdateStmt& source) noexcept
                                -> TargetForInitializerValue {
                                return TargetUpdateStmt {
                                    .op = source.op,
                                    .target =
                                        clone_expression_occurrence(source.target, active_path),
                                };
                            },
                        },
                        source_initializer.value
                    );
                    return TargetForInitializer {
                        .value = std::move(initializer_value),
                    };
                });
                auto steps = std::vector<TargetForStep>();
                steps.reserve(current.steps.size());
                for (const auto& source_step : current.steps) {
                    auto step_value = std::visit(
                        Overloaded {
                            [&](const TargetExprStmt& source) noexcept -> TargetForStepValue {
                                return TargetExprStmt {
                                    .expression =
                                        clone_expression_occurrence(source.expression, active_path),
                                };
                            },
                            [&](const TargetDiscardStmt& source) noexcept -> TargetForStepValue {
                                return TargetDiscardStmt {
                                    .expression =
                                        clone_expression_occurrence(source.expression, active_path),
                                };
                            },
                            [&](const TargetAssignmentStmt& source) noexcept -> TargetForStepValue {
                                return TargetAssignmentStmt {
                                    .target =
                                        clone_expression_occurrence(source.target, active_path),
                                    .op = source.op,
                                    .value = clone_expression_occurrence(source.value, active_path),
                                };
                            },
                            [&](const TargetUpdateStmt& source) noexcept -> TargetForStepValue {
                                return TargetUpdateStmt {
                                    .op = source.op,
                                    .target =
                                        clone_expression_occurrence(source.target, active_path),
                                };
                            },
                        },
                        source_step.value
                    );
                    steps.push_back({
                        .value = std::move(step_value),
                    });
                }
                return TargetForStmt {
                    .initializer = std::move(initializer),
                    .condition =
                        current.condition.transform([&](TargetExprID expression_id) noexcept {
                            return clone_expression_occurrence(expression_id, active_path);
                        }),
                    .steps = std::move(steps),
                    .body = clone_statement_occurrences(current.body, active_path),
                };
            },
            [&](const TargetRangeForStmt& current) noexcept -> TargetStmtValue {
                return TargetRangeForStmt {
                    .binding_mode = current.binding_mode,
                    .name = current.name,
                    .type = current.type,
                    .iterable = clone_expression_occurrence(current.iterable, active_path),
                    .body = clone_statement_occurrences(current.body, active_path),
                    .maybe_unused = current.maybe_unused,
                };
            },
        },
        source.value
    );
    active = 0u;
    return append_statement({
        .value = std::move(value),
        .attribution = source.attribution,
    });
}

auto TargetUnitBuilder::append_statement(TargetStmt statement) noexcept -> TargetStmtID {
    if (!statement.attribution.origin.has_value() && !statement.attribution.reason.has_value()) {
        invariant_violation("target statement append requires explicit attribution");
    }
    return storage.statements.add(std::move(statement));
}

auto TargetUnitBuilder::append_lowering_statement(TargetStmtValue value) noexcept -> TargetStmtID {
    return storage.statements.add({
        .value = std::move(value),
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::LoweringSupport,
        },
    });
}

auto TargetUnitBuilder::append_item(TargetItem item) noexcept -> TargetItemID {
    if (!item.attribution.origin.has_value() && !item.attribution.reason.has_value()) {
        invariant_violation("target item append requires explicit attribution");
    }
    return storage.items.add(std::move(item));
}

auto TargetUnitBuilder::append_lowering_item(TargetItemValue value) noexcept -> TargetItemID {
    return storage.items.add({
        .value = std::move(value),
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::LoweringSupport,
        },
    });
}

auto TargetUnitBuilder::expression(TargetExprID id) const noexcept -> const TargetExpr& {
    return storage.expressions.get(id);
}

auto TargetUnitBuilder::statement(TargetStmtID id) const noexcept -> const TargetStmt& {
    return storage.statements.get(id);
}

auto TargetUnitBuilder::item(TargetItemID id) const noexcept -> const TargetItem& {
    return storage.items.get(id);
}

auto TargetUnitBuilder::type(TargetTypeID id) const noexcept -> const TargetType& {
    return storage.types.get(id);
}

auto TargetUnitBuilder::finish(TargetUnitRoot root) && noexcept -> TargetUnit {
    auto unit = TargetUnit(std::move(storage), std::move(root));
    const auto validation = validate_target_unit({
        .types = unit.types(),
        .expressions = unit.expressions(),
        .statements = unit.statements(),
        .items = unit.items(),
        .root = unit.root(),
    });
    if (!validation.has_value()) {
        invariant_violation(validation.error().message);
    }
    return unit;
}
