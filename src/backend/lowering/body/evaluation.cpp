module carven:backend.lowering.body.evaluation.impl;

import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.traversal;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto may_require_lifetime_scope(
    const SemIRProgram& semantic,
    const SemanticExpression& expression
) noexcept -> bool {
    const auto& type = semantic.types().type(expression.type.resolved()).value;
    if (std::holds_alternative<BuiltinTypeValue>(type)
        || std::holds_alternative<SemCallable>(expression.value)
        || std::holds_alternative<SemEnumConstructor>(expression.value)) {
        return false;
    }
    const auto* enumeration = std::get_if<EnumTypeValue>(&type);
    return enumeration == nullptr
        || !std::holds_alternative<NumericEnumRepresentation>(
               semantic.declarations().enumeration(enumeration->enumeration).representation
        );
}

} // namespace

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
        || !facts(source).needs_lifetime_scope
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

auto BodyLowerer::expression_region(const SemanticExpression& source, ResultDemand demand) noexcept
    -> Lowered<LoweringResult> {
    auto statements = LoweringStmtBuilder();
    if (demand == ResultDemand::Discard || context.is_void(source.type.resolved())) {
        structured_expression(source, LoweringDiscardResult {}, statements);
        return std::move(statements)
            .complete<LoweringResult>(
                statements.continues() ? std::optional<LoweringResult>(LoweringCompleted {})
                                       : std::nullopt
            );
    }
    if (!facts(source).may_exit_value_region) {
        const auto yield = exit_target(LoweringExitKind::Value);
        structured_expression(source, LoweringYieldResult {.target = yield}, statements);
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
    structured_expression(source, LoweringInitializeResult {.storage = storage}, statements);
    auto destination = LoweringStmtBuilder();
    if (statements.continues()) {
        declare_deferred(
            storage,
            false,
            conditional_temporaries ? *conditional_temporaries : destination
        );
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
    ResultDemand demand,
    bool materializing
) noexcept -> Lowered<LoweringResult> {
    if (conditional_temporaries == nullptr
        && std::holds_alternative<SemShortCircuit>(source.value)
        && evaluation_form(source) == EvaluationForm::Statements) {
        auto storage = LoweringStmtBuilder();
        conditional_temporaries = &storage;
        auto lowered = expression_impl(source, literal, demand, materializing);
        conditional_temporaries = nullptr;
        auto result = storage.accept(std::move(lowered));
        return std::move(storage).complete<LoweringResult>(std::move(result));
    }
    auto destination = LoweringStmtBuilder();
    auto result = destination.accept(expression_impl(source, literal, demand, materializing));
    const auto producer = source.category != SemanticValueCategory::Place
        && !std::holds_alternative<SemBinding>(source.value)
        && !std::holds_alternative<SemDereference>(source.value)
        && !std::holds_alternative<SemField>(source.value)
        && !std::holds_alternative<SemIndex>(source.value)
        && !std::holds_alternative<SemCallable>(source.value)
        && !std::holds_alternative<SemEnumConstructor>(source.value);
    if (result
        && materializing
        && demand != ResultDemand::Discard
        && producer
        && std::holds_alternative<LoweringDirectExpression>(*result)
        && !std::holds_alternative<BuiltinTypeValue>(
            context.semantic().types().type(source.type.resolved()).value
        )) {
        result = LoweringTemporaryValue {materialize_temporary(
            {names.fresh(TargetTemporaryNameKind::Owner),
             context.lower_type(source.type.resolved())},
            require_expression(std::move(*result)),
            destination
        )};
    }
    return std::move(destination).complete<LoweringResult>(std::move(result));
}

auto BodyLowerer::expression_impl(
    const SemanticExpression& source,
    LoweringLiteralContext literal,
    ResultDemand demand,
    bool materializing
) noexcept -> Lowered<LoweringResult> {
    if (const auto* propagation = std::get_if<SemPropagate>(&source.value)) {
        return expression(*propagation->operand, literal, demand, materializing);
    }
    auto destination = LoweringStmtBuilder();
    if (source.constant
        && demand != ResultDemand::Discard
        && facts(source).rule.action != EvaluationAction::Required
        && !facts(source).needs_lifetime_scope) {
        static_cast<void>(destination.accept(retain_evaluation(source)));
        return std::move(destination)
            .complete<LoweringResult>(
                destination.continues()
                    ? std::optional<LoweringResult>(LoweringDirectExpression {
                          constant_expression(context, *source.constant, literal)
                      })
                    : std::nullopt
            );
    }
    if (const auto* logic = std::get_if<SemShortCircuit>(&source.value)) {
        const auto known = ::known_boolean(context.semantic(), *logic->left);
        if (known) {
            static_cast<void>(destination.accept(retain_evaluation(*logic->left)));
            if (!destination.continues()) {
                return std::move(destination).complete<LoweringResult>(std::nullopt);
            }
            if (*known == (logic->operation == ShortCircuitOperator::And)) {
                auto value =
                    destination.accept(expression(*logic->right, literal, demand, materializing));
                return std::move(destination).complete<LoweringResult>(std::move(value));
            }
            return std::move(destination)
                .complete<LoweringResult>(
                    demand == ResultDemand::Discard ? LoweringResult(LoweringCompleted {})
                                                    : LoweringResult(LoweringKnownBool {*known})
                );
        }
        const auto expand = evaluation_form(source) == EvaluationForm::Statements;
        auto left = destination.accept(expression(
            *logic->left,
            LoweringLiteralContext::Exact,
            ResultDemand::Observe,
            materializing || expand
        ));
        if (!left) {
            return std::move(destination).complete<LoweringResult>(std::nullopt);
        }
        if (!expand) {
            auto right =
                destination.accept(expression(*logic->right, literal, demand, materializing));
            if (!right) {
                return std::move(destination).complete<LoweringResult>(std::nullopt);
            }
            auto combined = binary_expression(
                require_expression(std::move(*left)),
                logic->operation == ShortCircuitOperator::And ? TargetBinaryOperator::LogicalAnd
                                                              : TargetBinaryOperator::LogicalOr,
                require_expression(std::move(*right))
            );
            return std::move(destination)
                .complete<LoweringResult>(LoweringDirectExpression {std::move(combined)});
        }
        auto result = LoweringResultDestination(LoweringDiscardResult {});
        auto normal = LoweringResult(LoweringCompleted {});
        if (demand != ResultDemand::Discard) {
            const auto name = names.fresh(TargetTemporaryNameKind::Logic);
            destination.emit(generated_statement(
                TargetVariableStmt {
                    .binding = TargetVariableBinding::MutableValue,
                    .maybe_unused = false,
                    .name = name,
                    .type = context.lower_type(source.type.resolved()),
                    .initializer = bool_expression(logic->operation == ShortCircuitOperator::Or)
                }
            ));
            result = LoweringBooleanResult {.name = name};
            normal = LoweringDirectExpression {name_expression(name)};
        }
        auto selected = LoweringStmtBuilder();
        auto right = selected.accept(expression(
            *logic->right,
            literal,
            demand == ResultDemand::Discard ? ResultDemand::Discard : ResultDemand::Observe,
            true
        ));
        if (right) {
            deliver_result(std::move(*right), result, selected);
        }
        auto predicate = require_expression(std::move(*left), LoweringResultUse::Observe);
        if (logic->operation == ShortCircuitOperator::Or) {
            predicate = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(predicate));
        }
        destination.record_exits(selected.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = std::move(predicate), .body = std::move(selected).finish()}
        );
        destination.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
        return std::move(destination).complete<LoweringResult>(std::move(normal));
    }
    if (std::holds_alternative<SemIf>(source.value)
        || std::holds_alternative<SemMatch>(source.value)
        || std::holds_alternative<SemTry>(source.value)) {
        return expression_region(source, demand);
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
    auto operands = destination.accept(
        lower_operands(operation_operands(source), operand_literal, materializing)
    );
    if (!operands) {
        return std::move(destination).complete<LoweringResult>(std::nullopt);
    }
    auto result = destination.accept(construct_operation(source, std::move(*operands), demand));

    return std::move(destination).complete<LoweringResult>(std::move(result));
}

auto BodyLowerer::retain_evaluation(const SemanticExpression& source) noexcept
    -> Lowered<LoweringCompleted> {
    auto destination = LoweringStmtBuilder();
    [&]() noexcept -> void {
        if (!destination.continues()) {
            return;
        }
        const auto rule = facts(source).rule;
        if (rule.action == EvaluationAction::None) {
            return;
        }
        if (rule.action != EvaluationAction::Required && !facts(source).needs_lifetime_scope) {
            if (rule.action == EvaluationAction::Operands) {
                for (const auto* operand : rule.operands) {
                    if (operand != nullptr) {
                        static_cast<void>(destination.accept(retain_evaluation(*operand)));
                    }
                }
                return;
            }
            if (!facts(*rule.operands[1]).requires_execution) {
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
        auto value = destination.accept(
            expression(source, LoweringLiteralContext::Exact, ResultDemand::Discard)
        );
        if (value) {
            deliver_result(std::move(*value), LoweringDiscardResult {}, destination);
        }
    }();
    return std::move(destination)
        .complete<LoweringCompleted>(
            destination.continues() ? std::optional(LoweringCompleted {}) : std::nullopt
        );
}

auto BodyLowerer::prepare_evaluation() noexcept -> void {
    struct Preparation final {
        BodyLowerer& lowering;
        std::vector<bool> lifetime_scopes;

        auto operator()(const SemanticExpression& expression) noexcept -> void {
            lifetime_scopes.push_back(
                may_require_lifetime_scope(lowering.context.semantic(), expression)
            );
            if (const auto* take = std::get_if<SemTake>(&expression.value)) {
                const auto* binding = std::get_if<SemBinding>(&take->place->value);
                if (!binding) {
                    invariant_violation("take source is not a complete owner binding");
                }
                lowering.taken_bindings.insert(binding->binding);
            }
        }

        auto leave(const SemanticExpression& expression) noexcept -> void {
            const bool lifetime_scope = lifetime_scopes.back();
            lifetime_scopes.pop_back();
            if (!lifetime_scopes.empty()) {
                lifetime_scopes.back() = lifetime_scopes.back() || lifetime_scope;
            }
            const auto& program = lowering.context.semantic();
            const auto rule = evaluation_rule(program, expression);
            const auto required = rule.action == EvaluationAction::Required;
            auto facts = EvaluationFacts {
                .rule = rule,
                .requires_execution = required,
                .reads_storage = required || std::holds_alternative<SemBinding>(expression.value),
                .needs_lifetime_scope = required && lifetime_scope,
                // Value branches cannot transfer to an enclosing callable or loop.
                .may_exit_value_region = expression.exits_test
                    || !program.failure_sets()
                            .failure_set(expression.failures.resolved())
                            .members.empty(),
                .form = EvaluationForm::Expression
            };
            for (const auto* operand : rule.operands) {
                if (operand) {
                    const auto& child = lowering.facts(*operand);
                    facts.requires_execution |= child.requires_execution;
                    facts.reads_storage |= child.reads_storage;
                    facts.needs_lifetime_scope |= child.needs_lifetime_scope;
                }
            }
            auto& stored =
                lowering.evaluation_facts.emplace(std::addressof(expression), facts).first->second;
            stored.form = lowering.classify_evaluation(expression);
        }
    };

    visit_semantic_nodes(body.region(), Preparation {*this, {}});
}
