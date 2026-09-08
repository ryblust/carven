module carven:backend.lowering.body.evaluation.impl;

import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

auto predicate_expression(LoweringPredicate predicate) noexcept -> TargetExpr {
    if (const auto* known = std::get_if<LoweringKnownBool>(&predicate)) {
        return bool_expression(known->value);
    }
    return std::move(std::get<LoweringDynamicBool>(predicate).expression);
}

auto remaining_expression(LoweringResult value, LoweringResultUse use) noexcept
    -> std::optional<TargetExpr> {
    return std::visit(
        Overloaded {
            [](LoweringCompleted) static noexcept -> std::optional<TargetExpr> {
                return std::nullopt;
            },
            [](LoweringKnownBool value) static noexcept -> std::optional<TargetExpr> {
                return bool_expression(value.value);
            },
            [](LoweringDirectExpression value) static noexcept -> std::optional<TargetExpr> {
                return std::move(value.expression);
            },
            [&](LoweringTemporaryValue value) noexcept -> std::optional<TargetExpr> {
                return use == LoweringResultUse::Transfer
                    ? transfer_expression(std::move(value.storage))
                    : std::move(value.storage);
            },
            [&](LoweringOwnedValue value) noexcept -> std::optional<TargetExpr> {
                return use == LoweringResultUse::Transfer
                    ? transfer_expression(std::move(value.storage))
                    : std::move(value.storage);
            }
        },
        std::move(value)
    );
}

auto require_expression(LoweringResult result, LoweringResultUse use) noexcept -> TargetExpr {
    auto expression = remaining_expression(std::move(result), use);
    if (!expression) {
        invariant_violation("completed evaluation reached an expression consumer");
    }
    return std::move(*expression);
}

auto BodyLowerer::read_value(
    Lowered<LoweringResult> evaluation,
    LoweringStmtBuilder& destination,
    LoweringResultUse use
) noexcept -> std::optional<TargetExpr> {
    auto result = destination.accept(std::move(evaluation));
    if (!result) {
        return std::nullopt;
    }
    return require_expression(std::move(*result), use);
}

auto BodyLowerer::full_expression(
    const SemanticExpression& source,
    LoweringLiteralContext use
) noexcept -> Lowered<LoweringResult> {
    auto lowered = expression(source, use);
    if (!lowered.normal
        || lowered.statements.empty()
        || !evaluation_preserves_full_expression(context.semantic(), source)
        || std::ranges::any_of(
            lowered.exits.targets,
            [](LoweringExitTarget target) static noexcept {
                return target.kind != LoweringExitKind::Unreachable;
            }
        )) {
        return lowered;
    }
    auto statements = LoweringStmtBuilder();
    auto value = statements.accept(std::move(lowered));
    const auto yield = exit_target(LoweringExitKind::Value);
    deliver_result(std::move(*value), LoweringYieldResult {.target = yield}, statements);
    auto expression =
        std::move(statements).result_region(context.lower_type(source.type.resolved()), yield);
    return std::move(LoweringStmtBuilder())
        .complete<LoweringResult>(LoweringDirectExpression {std::move(expression)});
}

auto BodyLowerer::initialize_deferred(
    const LoweringDeferredStorage& storage,
    TargetExpr initializer,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto* construction = std::get_if<TargetConstructionExpr>(&initializer.value);
    if (construction == nullptr || construction->type != storage.value_type) {
        // A typed return preserves copy-initialization before placement construction.
        const auto yield = exit_target(LoweringExitKind::Value);
        auto value = LoweringStmtBuilder();
        emit_return(std::move(initializer), value, LoweringYieldResult {.target = yield});
        initializer = std::move(value).result_region(storage.value_type, yield);
    }
    const auto address = names.fresh(TargetTemporaryNameKind::Operand);
    auto body = std::vector<TargetStmt>();
    body.push_back(generated_statement(
        TargetReturnStmt {
            .expression = TargetExpr {
                .value = TargetPlacementNewExpr {
                    .type = storage.value_type,
                    .address = target_child(name_expression(address)),
                    .initializer = target_child(std::move(initializer))
                }
            }
        }
    ));
    auto factory = TargetExpr {
        .value = TargetLambdaExpr {
            .parameters =
                {{.name = address,
                  .type = context.pointer_type(context.intrinsic_type(TargetSymbol::Void))}},
            .result = context.pointer_type(storage.value_type),
            .body = std::move(body)
        }
    };
    destination.emit(statement_expression(call_member(
        name_expression(storage.name),
        "initialize",
        target_expressions(std::move(factory))
    )));
}

auto BodyLowerer::external_exits(const SemanticExpression& source) const noexcept -> bool {
    if (source.exits_test
        || !context.plan().failure_abi().members(source.failures.resolved()).empty()) {
        return true;
    }
    auto exits = false;
    visit_semantic_nodes(source, [&](const SemanticStatement& statement) noexcept {
        exits |= std::holds_alternative<SemReturn>(statement.value)
            || std::holds_alternative<SemBreak>(statement.value)
            || std::holds_alternative<SemContinue>(statement.value);
    });
    return exits;
}

auto BodyLowerer::can_extend_branch_scope(const SemanticExpression& source) const noexcept -> bool {
    const auto* conditional = std::get_if<SemIf>(&source.value);
    if (conditional == nullptr) {
        return false;
    }
    const auto direct = [&](const SemanticRegion& region) noexcept {
        if (std::ranges::any_of(
                region.statements,
                [](const SemanticStatement& statement) static noexcept {
                    return std::holds_alternative<SemInitialize>(statement.value);
                }
            )) {
            return false;
        }
        return !region.result
            || std::ranges::none_of(
                operation_operands(*region.result).values,
                [&](const Operand& operand) noexcept {
                    return evaluation_preserves_full_expression(
                        context.semantic(),
                        operand.expression
                    );
                }
            );
    };
    return std::ranges::all_of(
               conditional->branches,
               [&](const auto& branch) noexcept { return direct(branch.body); }
           )
        && (!conditional->otherwise || direct(**conditional->otherwise));
}

auto BodyLowerer::expression_region(
    const SemanticExpression& source,
    ResultDemand demand,
    const std::function<void(LoweringResultDestination, LoweringStmtBuilder&)>& build
) noexcept -> Lowered<LoweringResult> {
    auto statements = LoweringStmtBuilder();
    if (demand == ResultDemand::Discard || context.is_void(source.type.resolved())) {
        build(LoweringDiscardResult {}, statements);
        return std::move(statements)
            .complete<LoweringResult>(
                statements.continues() ? std::optional<LoweringResult>(LoweringCompleted {})
                                       : std::nullopt
            );
    }
    if (!external_exits(source)) {
        const auto yield = exit_target(LoweringExitKind::Value);
        build(LoweringYieldResult {.target = yield}, statements);
        const auto returns = statements.exits().contains(yield);
        auto value =
            std::move(statements).result_region(context.lower_type(source.type.resolved()), yield);
        auto destination = LoweringStmtBuilder();
        if (!returns) {
            destination.terminate(
                statement_expression(std::move(value)),
                LoweringExitTarget {LoweringExitKind::Unreachable, 0}
            );
            return std::move(destination).complete<LoweringResult>(std::nullopt);
        }
        return std::move(destination)
            .complete<LoweringResult>(LoweringDirectExpression {std::move(value)});
    }
    const auto storage = LoweringDeferredStorage {
        .name = names.fresh(TargetTemporaryNameKind::Owner),
        .value_type = context.lower_type(source.type.resolved())
    };
    build(LoweringInitializeResult {.storage = storage}, statements);
    auto destination = LoweringStmtBuilder();
    if (statements.continues()) {
        declare_deferred(storage, false, destination);
    }
    destination.append(std::move(statements));
    return std::move(destination)
        .complete<LoweringResult>(
            destination.continues() ? std::optional<LoweringResult>(LoweringTemporaryValue {
                                          dereference_expression(name_expression(storage.name))
                                      })
                                    : std::nullopt
        );
}

auto BodyLowerer::expression(
    const SemanticExpression& source,
    LoweringLiteralContext literal,
    ResultDemand demand
) noexcept -> Lowered<LoweringResult> {
    if (evaluation_form(source) == EvaluationForm::Branches) {
        return expression_region(
            source,
            demand,
            [&](LoweringResultDestination result, LoweringStmtBuilder& destination) noexcept {
                consume_expression(
                    source,
                    literal,
                    demand,
                    [&](LoweringResult value, LoweringStmtBuilder& branch) noexcept {
                        deliver_result(std::move(value), result, branch);
                    },
                    destination
                );
            }
        );
    }
    auto destination = LoweringStmtBuilder();
    auto result = std::optional<LoweringResult>();
    consume_expression(
        source,
        literal,
        demand,
        [&](LoweringResult value, LoweringStmtBuilder&) noexcept { result = std::move(value); },
        destination
    );
    return std::move(destination).complete<LoweringResult>(std::move(result));
}

auto BodyLowerer::consume_expression(
    const SemanticExpression& source,
    LoweringLiteralContext literal,
    ResultDemand demand,
    const LoweringResultConsumer& consume,
    LoweringStmtBuilder& destination,
    bool materializing
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (const auto* propagation = std::get_if<SemPropagate>(&source.value)) {
        consume_expression(
            *propagation->operand,
            literal,
            demand,
            consume,
            destination,
            materializing
        );
        return;
    }
    if (source.constant
        && demand != ResultDemand::Discard
        && evaluation_rule(context.semantic(), source).action != EvaluationAction::Required
        && !evaluation_preserves_full_expression(context.semantic(), source)) {
        static_cast<void>(destination.accept(retain_evaluation(source)));
        if (destination.continues()) {
            consume(
                LoweringDirectExpression {constant_expression(context, *source.constant, literal)},
                destination
            );
        }
        return;
    }
    if (const auto* logic = std::get_if<SemShortCircuit>(&source.value)) {
        const auto known = ::known_boolean(context.semantic(), *logic->left);
        if (known) {
            static_cast<void>(destination.accept(retain_evaluation(*logic->left)));
            if (!destination.continues()) {
                return;
            }
            if (*known == (logic->operation == ShortCircuitOperator::And)) {
                consume_expression(
                    *logic->right,
                    literal,
                    demand,
                    consume,
                    destination,
                    materializing
                );
            } else {
                consume(
                    demand == ResultDemand::Discard ? LoweringResult(LoweringCompleted {})
                                                    : LoweringResult(LoweringKnownBool {*known}),
                    destination
                );
            }
            return;
        }
        const auto expand = evaluation_form(source) == EvaluationForm::Branches;
        consume_expression(
            *logic->left,
            LoweringLiteralContext::Exact,
            ResultDemand::Observe,
            [&](LoweringResult left, LoweringStmtBuilder& branch) noexcept {
                if (const auto* known = std::get_if<LoweringKnownBool>(&left)) {
                    if (known->value == (logic->operation == ShortCircuitOperator::And)) {
                        consume_expression(
                            *logic->right,
                            literal,
                            demand,
                            consume,
                            branch,
                            materializing
                        );
                    } else {
                        consume(
                            demand == ResultDemand::Discard ? LoweringResult(LoweringCompleted {})
                                                            : std::move(left),
                            branch
                        );
                    }
                    return;
                }
                if (!expand) {
                    auto right = expression(*logic->right);
                    auto value = branch.accept(std::move(right));
                    if (value) {
                        auto combined = binary_expression(
                            require_expression(std::move(left)),
                            logic->operation == ShortCircuitOperator::And
                                ? TargetBinaryOperator::LogicalAnd
                                : TargetBinaryOperator::LogicalOr,
                            require_expression(std::move(*value))
                        );
                        consume(LoweringDirectExpression {std::move(combined)}, branch);
                    }
                    return;
                }
                auto selected = LoweringStmtBuilder();
                auto skipped = LoweringStmtBuilder();
                consume_expression(
                    *logic->right,
                    literal,
                    demand,
                    consume,
                    selected,
                    materializing
                );
                consume(
                    demand == ResultDemand::Discard
                        ? LoweringResult(LoweringCompleted {})
                        : LoweringResult(
                              LoweringKnownBool {logic->operation == ShortCircuitOperator::Or}
                          ),
                    skipped
                );
                auto condition = require_expression(std::move(left), LoweringResultUse::Observe);
                if (logic->operation == ShortCircuitOperator::Or) {
                    condition =
                        prefix_expression(TargetPrefixOperator::LogicalNot, std::move(condition));
                }
                const auto continues = selected.continues() || skipped.continues();
                branch.record_exits(selected.exits());
                branch.record_exits(skipped.exits());
                auto alternatives = std::vector<TargetIfBranch>();
                alternatives.push_back(
                    {.condition = std::move(condition), .body = std::move(selected).finish()}
                );
                branch.emit(
                    generated_statement(
                        TargetIfStmt {
                            .branches = std::move(alternatives),
                            .else_body = skipped.empty()
                                ? std::nullopt
                                : std::optional(std::move(skipped).finish())
                        }
                    ),
                    continues
                );
            },
            destination,
            expand
        );
        return;
    }
    if (std::holds_alternative<SemIf>(source.value)
        || std::holds_alternative<SemMatch>(source.value)
        || std::holds_alternative<SemTry>(source.value)) {
        if (demand != ResultDemand::Discard && can_extend_branch_scope(source)) {
            structured_expression(source, LoweringConsumeResult {.consume = consume}, destination);
            return;
        }
        auto value = destination.accept(expression_region(
            source,
            demand,
            [&](LoweringResultDestination result, LoweringStmtBuilder& statements) noexcept {
                structured_expression(source, std::move(result), statements);
            }
        ));
        if (value) {
            consume(std::move(*value), destination);
        }
        return;
    }
    auto operand_literal = LoweringLiteralContext::Exact;
    if (std::holds_alternative<SemCall>(source.value)) {
        operand_literal = LoweringLiteralContext::TargetTyped;
    }
    if (const auto* unary = std::get_if<SemUnary>(&source.value); unary != nullptr
        && unary->operation == UnaryOperator::Negate
        && context.is_integer(source.type.resolved())) {
        operand_literal = LoweringLiteralContext::TargetTyped;
    }
    if (const auto* binary = std::get_if<SemBinary>(&source.value); binary != nullptr
        && context.is_integer(source.type.resolved())
        && binary->operation != BinaryOperator::BitwiseAnd
        && binary->operation != BinaryOperator::BitwiseOr
        && binary->operation != BinaryOperator::BitwiseXor
        && binary->operation != BinaryOperator::LeftShift
        && binary->operation != BinaryOperator::RightShift) {
        operand_literal = LoweringLiteralContext::TargetTyped;
    }
    consume_operands(
        operation_operands(source),
        operand_literal,
        materializing,
        [&](std::vector<TargetExpr> operands, LoweringStmtBuilder& branch) noexcept {
            auto result = branch.accept(construct_operation(source, std::move(operands), demand));
            if (!result) {
                return;
            }
            const auto producer = source.category != SemanticValueCategory::Place
                && !std::holds_alternative<SemBinding>(source.value)
                && !std::holds_alternative<SemField>(source.value)
                && !std::holds_alternative<SemIndex>(source.value)
                && !std::holds_alternative<SemCallable>(source.value)
                && !std::holds_alternative<SemEnumConstructor>(source.value);
            if (materializing
                && demand != ResultDemand::Discard
                && producer
                && std::holds_alternative<LoweringDirectExpression>(*result)
                && !std::holds_alternative<BuiltinTypeValue>(
                    context.semantic().types().type(source.type.resolved()).value
                )) {
                const auto owner = names.fresh(TargetTemporaryNameKind::Owner);
                branch.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::RvalueReference,
                        .maybe_unused = false,
                        .name = owner,
                        .type = context.intrinsic_type(TargetSymbol::Auto),
                        .initializer = require_expression(std::move(*result))
                    }
                ));
                result = LoweringTemporaryValue {name_expression(owner)};
            }
            consume(std::move(*result), branch);
        },
        destination
    );
}

auto BodyLowerer::retain_evaluation(const SemanticExpression& source) noexcept
    -> Lowered<LoweringCompleted> {
    auto destination = LoweringStmtBuilder();
    [&]() noexcept -> void {
        if (!destination.continues()) {
            return;
        }
        const auto rule = evaluation_rule(context.semantic(), source);
        if (rule.action == EvaluationAction::None) {
            return;
        }
        if (rule.action != EvaluationAction::Required
            && !evaluation_preserves_full_expression(context.semantic(), source)) {
            if (rule.action == EvaluationAction::Operands) {
                for (const auto* operand : rule.operands) {
                    if (operand != nullptr) {
                        static_cast<void>(destination.accept(retain_evaluation(*operand)));
                    }
                }
                return;
            }
            if (!evaluation_requires_execution(context.semantic(), *rule.operands[1])) {
                static_cast<void>(destination.accept(retain_evaluation(*rule.operands[0])));
                return;
            }
            auto test = destination.accept(condition(*rule.operands[0]));
            if (!destination.continues()) {
                return;
            }
            const auto* logic = std::get_if<SemShortCircuit>(&source.value);
            if (logic == nullptr) {
                invariant_violation("short-circuit evaluation rule requires a logical operation");
            }
            auto selected = LoweringStmtBuilder();
            static_cast<void>(selected.accept(retain_evaluation(*rule.operands[1])));
            auto predicate = predicate_expression(std::move(*test));
            if (logic->operation == ShortCircuitOperator::Or) {
                predicate =
                    prefix_expression(TargetPrefixOperator::LogicalNot, std::move(predicate));
            }
            destination.record_exits(selected.exits());
            auto branches = std::vector<TargetIfBranch>();
            branches.push_back(
                {.condition = std::move(predicate), .body = std::move(selected).finish()}
            );
            destination.emit(generated_statement(
                TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
            ));
            return;
        }
        consume_expression(
            source,
            LoweringLiteralContext::Exact,
            ResultDemand::Discard,
            [&](LoweringResult result, LoweringStmtBuilder& branch) noexcept {
                deliver_result(std::move(result), LoweringDiscardResult {}, branch);
            },
            destination
        );
    }();
    return std::move(destination)
        .complete<LoweringCompleted>(
            destination.continues() ? std::optional(LoweringCompleted {}) : std::nullopt
        );
}
