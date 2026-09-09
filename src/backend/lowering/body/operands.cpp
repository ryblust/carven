module carven:backend.lowering.body.operands.impl;

import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.target.expr;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyLowerer::operation_operands(const SemanticExpression& source) const noexcept
    -> OperandGroup {
    auto group = OperandGroup {.values = {}, .order = OperandOrder::Unspecified};
    const auto access = [&](const SemCallArgument& argument) noexcept {
        if (argument.access == AccessMode::Write) {
            return OperandUse::Place;
        }
        if (argument.access == AccessMode::Take) {
            return OperandUse::Own;
        }
        return std::holds_alternative<PointerTypeValue>(
                   context.semantic().types().type(argument.expression.type.resolved()).value
               )
            ? OperandUse::Snapshot
            : OperandUse::Read;
    };
    std::visit(
        Overloaded {
            [&](const SemArray& array) noexcept {
                group.order = OperandOrder::LeftToRight;
                for (const auto& value : array.elements) {
                    group.values.push_back({value, OperandUse::Own});
                }
            },
            [&](const SemStruct& structure) noexcept {
                group.order = std::ranges::is_sorted(
                                  structure.fields,
                                  {},
                                  &SemFieldInitializer::declaration_index
                              )
                    ? OperandOrder::LeftToRight
                    : OperandOrder::Reordered;
                for (const auto& field : structure.fields) {
                    group.values.push_back({field.value, OperandUse::Own});
                }
            },
            [&](const SemEnumCase& enumeration) noexcept {
                for (const auto& value : enumeration.payload) {
                    group.values.push_back({value, OperandUse::Own});
                }
            },
            [&](const SemClosure& closure) noexcept {
                group.order = OperandOrder::LeftToRight;
                for (const auto& value : closure.captures) {
                    group.values.push_back(
                        {value.expression,
                         value.mode == CaptureMode::Write ? OperandUse::Place : OperandUse::Own}
                    );
                }
            },
            [&](const SemBinary& binary) noexcept {
                group.values.push_back({*binary.left, OperandUse::Snapshot});
                group.values.push_back({*binary.right, OperandUse::Snapshot});
            },
            [&](const SemIndex& index) noexcept {
                group.values.push_back({*index.source, OperandUse::Place});
                group.values.push_back({*index.index, OperandUse::Snapshot});
            },
            [&](const SemCall& call) noexcept {
                group.order = OperandOrder::Postfix;
                const auto closure = std::holds_alternative<ClosureTypeValue>(
                    context.semantic().types().type(call.callee->type.resolved()).value
                );
                group.values.push_back(
                    {*call.callee, closure ? OperandUse::ConstPlace : OperandUse::Snapshot}
                );
                for (const auto& value : call.arguments) {
                    group.values.push_back({value.expression, access(value)});
                }
            },
            [&](const SemCppCall& call) noexcept {
                const auto receiver = [&](const SemCppOperand& value) noexcept {
                    group.order = OperandOrder::Postfix;
                    group.values.push_back(
                        {*value.expression,
                         value.access == AccessMode::Write ? OperandUse::Place
                                                           : OperandUse::ConstPlace}
                    );
                };
                std::visit(
                    Overloaded {
                        [](const CppNameReference&) static noexcept {},
                        [&](const CppMemberCallee<SemCppOperand>& value) noexcept {
                            receiver(value.receiver);
                        },
                        receiver
                    },
                    call.callee
                );
                for (const auto& value : call.arguments) {
                    group.values.push_back({value.expression, access(value)});
                }
            },
            [&](const SemCpp& operation) noexcept {
                group.order = std::holds_alternative<CppConstructOperation>(operation.operation)
                    ? OperandOrder::LeftToRight
                    : OperandOrder::Unspecified;
                for (const auto& value : operation.operands) {
                    const auto receiver = group.values.empty()
                        && (std::holds_alternative<CppMemberOperation>(operation.operation)
                            || std::holds_alternative<CppIndexOperation>(operation.operation));
                    group.values.push_back(
                        {value.expression,
                         receiver ? (value.access == AccessMode::Write ? OperandUse::Place
                                                                       : OperandUse::ConstPlace)
                                  : access(value)}
                    );
                }
            },
            [&](const SemUnary& value) noexcept {
                group.values.push_back({*value.operand, OperandUse::Snapshot});
            },
            [&](const SemCast& value) noexcept {
                group.values.push_back({*value.operand, OperandUse::Snapshot});
            },
            [&](const SemDereference& value) noexcept {
                group.values.push_back({*value.source, OperandUse::Snapshot});
            },
            [&](const SemField& value) noexcept {
                group.values.push_back({*value.source, OperandUse::Place});
            },
            [&](const SemTextIntrinsic& value) noexcept {
                group.values.push_back({*value.source, OperandUse::Read});
            },
            [&](const SemTake& value) noexcept {
                group.values.push_back({*value.place, OperandUse::Place});
            },
            [&](const SemBorrowCallable& value) noexcept {
                group.values.push_back({*value.source, OperandUse::Place});
            },
            [&](const SemArrayAdopt& value) noexcept {
                group.values.push_back({*value.source, OperandUse::Place});
            },
            [](const auto&) static noexcept {}
        },
        source.value
    );
    return group;
}

auto BodyLowerer::requires_materialization(const OperandGroup& group, bool prefix) const noexcept
    -> bool {
    auto executions = 0uz;
    auto reads = false;
    auto takes = false;
    for (auto index = 0uz; index < group.values.size(); ++index) {
        const auto& source = group.values[index].expression;
        if (group.values[index].use == OperandUse::Snapshot
            && facts(source).reads_storage
            && std::holds_alternative<PointerTypeValue>(
                context.semantic().types().type(source.type.resolved()).value
            )) {
            return true;
        }
        takes |= std::holds_alternative<SemTake>(source.value);
        if (group.order == OperandOrder::Postfix
            && index == 0uz
            && group.values[index].use != OperandUse::Snapshot) {
            continue;
        }
        const auto executes = facts(source).requires_execution;
        executions += executes;
        reads |= !executes && facts(source).reads_storage;
    }
    return prefix
        || takes
        || group.order == OperandOrder::Reordered
        || ((group.order == OperandOrder::Unspecified || group.order == OperandOrder::Postfix)
            && (executions > 1uz || (executions == 1uz && reads)));
}

auto BodyLowerer::classify_evaluation(const SemanticExpression& source) const noexcept
    -> EvaluationForm {
    const auto folded = source.constant
        && facts(source).rule.action != EvaluationAction::Required
        && !facts(source).needs_lifetime_scope;
    if (folded && !facts(source).requires_execution) {
        return EvaluationForm::Expression;
    }
    if (const auto* propagation = std::get_if<SemPropagate>(&source.value)) {
        return evaluation_form(*propagation->operand);
    }
    if (const auto* logic = std::get_if<SemShortCircuit>(&source.value)) {
        return folded
                || evaluation_form(*logic->left) != EvaluationForm::Expression
                || evaluation_form(*logic->right) != EvaluationForm::Expression
                || facts(source).needs_lifetime_scope
            ? EvaluationForm::Statements
            : EvaluationForm::Expression;
    }
    if (std::holds_alternative<SemIf>(source.value)
        || std::holds_alternative<SemMatch>(source.value)
        || std::holds_alternative<SemTry>(source.value)) {
        return folded
                || context.is_void(source.type.resolved())
                || facts(source).may_exit_value_region
            ? EvaluationForm::Statements
            : EvaluationForm::Expression;
    }
    const auto group = operation_operands(source);
    auto children = EvaluationForm::Expression;
    for (const auto& operand : group.values) {
        children = std::max(children, evaluation_form(operand.expression));
    }
    if (const auto* borrow = std::get_if<SemBorrowCallable>(&source.value);
        borrow != nullptr && facts(*borrow->source).requires_execution) {
        return EvaluationForm::Statements;
    }
    if (std::holds_alternative<SemArrayAdopt>(source.value)) {
        return EvaluationForm::Statements;
    }
    if (const auto* call = std::get_if<SemCall>(&source.value); call != nullptr
        && !context.plan().failure_abi().members(call->callee_failures.resolved()).empty()) {
        return EvaluationForm::Statements;
    }
    return folded || requires_materialization(group, children == EvaluationForm::Statements)
        ? EvaluationForm::Statements
        : EvaluationForm::Expression;
}

auto BodyLowerer::lower_operands(
    const OperandGroup& group,
    LoweringLiteralContext literal,
    bool materializing
) noexcept -> Lowered<std::vector<TargetExpr>> {
    const auto prefix = std::ranges::any_of(group.values, [&](const Operand& operand) noexcept {
        return evaluation_form(operand.expression) == EvaluationForm::Statements;
    });
    const auto stabilize = requires_materialization(group, prefix);
    auto statements = LoweringStmtBuilder();
    auto values = std::vector<TargetExpr>();
    for (const auto& input : group.values) {
        auto evaluated = statements.accept(expression(
            input.expression,
            literal,
            input.use == OperandUse::Read || input.use == OperandUse::ConstPlace
                ? ResultDemand::Observe
                : ResultDemand::Value,
            materializing || stabilize
        ));
        if (!evaluated) {
            return std::move(statements).complete<std::vector<TargetExpr>>(std::nullopt);
        }
        const auto temporary = std::holds_alternative<LoweringTemporaryValue>(*evaluated);
        auto value = require_expression(
            std::move(*evaluated),
            input.use == OperandUse::Own || input.use == OperandUse::Snapshot
                ? LoweringResultUse::Transfer
                : LoweringResultUse::Observe
        );
        if (stabilize && (!temporary || input.use == OperandUse::Own)) {
            value = materialize_operand(input.expression, std::move(value), input.use, statements);
        } else if (input.use == OperandUse::ConstPlace
                   || (input.use == OperandUse::Read && facts(input.expression).reads_storage)) {
            value = TargetExpr {
                .value = TargetStaticCastExpr {
                    .type = context.reference_type(
                        context.lower_type(input.expression.type.resolved()),
                        true
                    ),
                    .operand = target_child(std::move(value))
                }
            };
        }
        values.push_back(std::move(value));
    }
    return std::move(statements).complete<std::vector<TargetExpr>>(std::move(values));
}
