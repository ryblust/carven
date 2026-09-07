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

namespace body_lowering {

auto predicate_expression(Predicate predicate) noexcept -> TargetExpr {
    if (const auto* known = std::get_if<KnownBool>(&predicate)) {
        return bool_expression(known->value);
    }
    return std::move(std::get<DynamicBool>(predicate).expression);
}

auto value_expression(Evaluated value, ValueUse use) noexcept -> TargetExpr {
    return std::visit(
        Overloaded {
            [](VoidResult) static noexcept -> TargetExpr {
                invariant_violation("void result reached a value consumer");
            },
            [](KnownBool value) static noexcept -> TargetExpr {
                return bool_expression(value.value);
            },
            [](DirectValue value) static noexcept -> TargetExpr {
                return std::move(value.expression);
            },
            [&](TemporaryValue value) noexcept -> TargetExpr {
                return use == ValueUse::Transfer ? transfer_expression(std::move(value.storage))
                                                 : std::move(value.storage);
            },
            [&](OwnedValue value) noexcept -> TargetExpr {
                return use == ValueUse::Transfer ? transfer_expression(std::move(value.storage))
                                                 : std::move(value.storage);
            }
        },
        std::move(value)
    );
}

auto BodyLowerer::read_value(
    Lowered<Evaluated> evaluation,
    StatementBuilder& destination,
    ValueUse use
) noexcept -> std::optional<TargetExpr> {
    auto result = destination.accept(std::move(evaluation));
    if (!result) {
        return std::nullopt;
    }
    return value_expression(std::move(*result), use);
}

auto BodyLowerer::full_expression(const SemanticExpression& source, LiteralContext use) noexcept
    -> Lowered<Evaluated> {
    auto lowered = expression(source, use);
    if (!lowered.normal
        || lowered.statements.empty()
        || !evaluation_preserves_full_expression(context.semantic(), source)
        || std::ranges::any_of(lowered.exits.targets, [](ExitTarget target) static noexcept {
               return target.kind != ExitKind::Unreachable;
           })) {
        return lowered;
    }
    auto statements = StatementBuilder();
    auto value = statements.accept(std::move(lowered));
    const auto yield = exit_target(ExitKind::Value);
    emit_return(value_expression(std::move(*value)), statements, YieldResult {.target = yield});
    auto expression =
        std::move(statements).result_region(context.lower_type(source.type.resolved()), yield);
    return std::move(StatementBuilder()).complete<Evaluated>(DirectValue {std::move(expression)});
}

auto BodyLowerer::initialize_deferred(
    const DeferredStorage& storage,
    TargetExpr initializer,
    StatementBuilder& destination
) noexcept -> void {
    const auto* construction = std::get_if<TargetConstructionExpr>(&initializer.value);
    if (construction == nullptr || construction->type != storage.value_type) {
        // A typed return preserves copy-initialization before placement construction.
        const auto yield = exit_target(ExitKind::Value);
        auto value = StatementBuilder();
        emit_return(std::move(initializer), value, YieldResult {.target = yield});
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

auto BodyLowerer::value_region(
    const SemanticExpression& source,
    const std::function<void(ResultDestination, StatementBuilder&)>& build
) noexcept -> Lowered<Evaluated> {
    auto statements = StatementBuilder();
    if (context.is_void(source.type.resolved())) {
        build(DiscardResult {}, statements);
        return std::move(statements)
            .complete<Evaluated>(
                statements.continues() ? std::optional<Evaluated>(VoidResult {}) : std::nullopt
            );
    }
    if (!external_exits(source)) {
        const auto yield = exit_target(ExitKind::Value);
        build(YieldResult {.target = yield}, statements);
        const auto returns = statements.exits().contains(yield);
        auto value =
            std::move(statements).result_region(context.lower_type(source.type.resolved()), yield);
        auto destination = StatementBuilder();
        if (!returns) {
            destination.terminate(
                statement_expression(std::move(value)),
                ExitTarget {ExitKind::Unreachable, 0}
            );
            return std::move(destination).complete<Evaluated>(std::nullopt);
        }
        return std::move(destination).complete<Evaluated>(DirectValue {std::move(value)});
    }
    const auto storage = DeferredStorage {
        .name = names.fresh(TargetTemporaryNameKind::Owner),
        .value_type = context.lower_type(source.type.resolved())
    };
    build(InitializeResult {.storage = storage}, statements);
    auto destination = StatementBuilder();
    if (statements.continues()) {
        declare_deferred(storage, false, destination);
    }
    destination.append(std::move(statements));
    return std::move(destination)
        .complete<Evaluated>(
            destination.continues() ? std::optional<Evaluated>(TemporaryValue {
                                          dereference_expression(name_expression(storage.name))
                                      })
                                    : std::nullopt
        );
}

auto BodyLowerer::expression(
    const SemanticExpression& source,
    LiteralContext literal,
    ResultDemand demand
) noexcept -> Lowered<Evaluated> {
    if (evaluation_form(source) == EvaluationForm::Branches && demand != ResultDemand::Discard) {
        return value_region(
            source,
            [&](ResultDestination result, StatementBuilder& destination) noexcept {
                consume_expression(
                    source,
                    literal,
                    demand,
                    [&](Evaluated value, StatementBuilder& branch) noexcept {
                        deliver_result(std::move(value), result, branch);
                    },
                    destination
                );
            }
        );
    }
    auto destination = StatementBuilder();
    auto result = std::optional<Evaluated>();
    consume_expression(
        source,
        literal,
        demand,
        [&](Evaluated value, StatementBuilder&) noexcept { result = std::move(value); },
        destination
    );
    return std::move(destination).complete<Evaluated>(std::move(result));
}

auto BodyLowerer::consume_expression(
    const SemanticExpression& source,
    LiteralContext literal,
    ResultDemand demand,
    const ValueConsumer& consume,
    StatementBuilder& destination,
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
                DirectValue {constant_expression(context, *source.constant, literal)},
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
                    demand == ResultDemand::Discard ? Evaluated(VoidResult {})
                                                    : Evaluated(KnownBool {*known}),
                    destination
                );
            }
            return;
        }
        const auto expand = evaluation_form(source) == EvaluationForm::Branches;
        consume_expression(
            *logic->left,
            LiteralContext::Exact,
            ResultDemand::Observe,
            [&](Evaluated left, StatementBuilder& branch) noexcept {
                if (const auto* known = std::get_if<KnownBool>(&left)) {
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
                            demand == ResultDemand::Discard ? Evaluated(VoidResult {})
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
                            value_expression(std::move(left)),
                            logic->operation == ShortCircuitOperator::And
                                ? TargetBinaryOperator::LogicalAnd
                                : TargetBinaryOperator::LogicalOr,
                            value_expression(std::move(*value))
                        );
                        if (demand == ResultDemand::Discard) {
                            branch.emit(generated_statement(
                                TargetDiscardStmt {.expression = std::move(combined)}
                            ));
                            consume(VoidResult {}, branch);
                        } else {
                            consume(DirectValue {std::move(combined)}, branch);
                        }
                    }
                    return;
                }
                auto selected = StatementBuilder();
                auto skipped = StatementBuilder();
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
                        ? Evaluated(VoidResult {})
                        : Evaluated(KnownBool {logic->operation == ShortCircuitOperator::Or}),
                    skipped
                );
                auto condition = value_expression(std::move(left), ValueUse::Observe);
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
        if (demand == ResultDemand::Discard || context.is_void(source.type.resolved())) {
            structured_expression(source, DiscardResult {}, destination);
            if (destination.continues()) {
                consume(VoidResult {}, destination);
            }
            return;
        }
        if (can_extend_branch_scope(source)) {
            structured_expression(source, ConsumeResult {.consume = consume}, destination);
            return;
        }
        auto value = destination.accept(value_region(
            source,
            [&](ResultDestination result, StatementBuilder& statements) noexcept {
                structured_expression(source, std::move(result), statements);
            }
        ));
        if (value) {
            consume(std::move(*value), destination);
        }
        return;
    }
    auto operand_literal = LiteralContext::Exact;
    if (std::holds_alternative<SemCall>(source.value)) {
        operand_literal = LiteralContext::TargetTyped;
    }
    if (const auto* unary = std::get_if<SemUnary>(&source.value); unary != nullptr
        && unary->operation == UnaryOperator::Negate
        && context.is_integer(source.type.resolved())) {
        operand_literal = LiteralContext::TargetTyped;
    }
    if (const auto* binary = std::get_if<SemBinary>(&source.value); binary != nullptr
        && context.is_integer(source.type.resolved())
        && binary->operation != BinaryOperator::BitwiseAnd
        && binary->operation != BinaryOperator::BitwiseOr
        && binary->operation != BinaryOperator::BitwiseXor
        && binary->operation != BinaryOperator::LeftShift
        && binary->operation != BinaryOperator::RightShift) {
        operand_literal = LiteralContext::TargetTyped;
    }
    consume_operands(
        operation_operands(source),
        operand_literal,
        materializing,
        [&](std::vector<TargetExpr> operands, StatementBuilder& branch) noexcept {
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
                && producer
                && std::holds_alternative<DirectValue>(*result)
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
                        .initializer = value_expression(std::move(*result))
                    }
                ));
                result = TemporaryValue {name_expression(owner)};
            }
            consume(std::move(*result), branch);
        },
        destination
    );
}

auto BodyLowerer::retain_evaluation(const SemanticExpression& source) noexcept -> Lowered<Unit> {
    auto destination = StatementBuilder();
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
            auto selected = StatementBuilder();
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
        static_cast<void>(
            destination.accept(expression(source, LiteralContext::Exact, ResultDemand::Discard))
        );
    }();
    return std::move(destination)
        .complete<Unit>(destination.continues() ? std::optional(Unit {}) : std::nullopt);
}

} // namespace body_lowering
