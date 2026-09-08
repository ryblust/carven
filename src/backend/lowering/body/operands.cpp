module carven:backend.lowering.body.operands.impl;

import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :semantic.semir;
import :backend.target.expr;
import :backend.target.symbol;
import :support.invariant;
import :support.visit;
import std;

auto BodyLowerer::operation_operands(const SemanticExpression& source) const noexcept
    -> OperandGroup {
    auto group = OperandGroup {.values = {}, .order = OperandOrder::Unspecified};
    const auto access = [](AccessMode mode) static noexcept {
        return mode == AccessMode::Write ? OperandUse::Place
            : mode == AccessMode::Take   ? OperandUse::Own
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
                    group.values.push_back({value.expression, access(value.access)});
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
                    group.values.push_back({value.expression, access(value.access)});
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
                                  : access(value.access)}
                    );
                }
            },
            [&](const SemUnary& value) noexcept {
                group.values.push_back({*value.operand, OperandUse::Snapshot});
            },
            [&](const SemCast& value) noexcept {
                group.values.push_back({*value.operand, OperandUse::Snapshot});
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
        takes |= std::holds_alternative<SemTake>(source.value);
        if (group.order == OperandOrder::Postfix
            && index == 0uz
            && group.values[index].use != OperandUse::Snapshot) {
            continue;
        }
        const auto executes = evaluation_requires_execution(context.semantic(), source);
        executions += executes;
        reads |= !executes && evaluation_reads_storage(context.semantic(), source);
    }
    return prefix
        || takes
        || group.order == OperandOrder::Reordered
        || ((group.order == OperandOrder::Unspecified || group.order == OperandOrder::Postfix)
            && (executions > 1uz || (executions == 1uz && reads)));
}

auto BodyLowerer::evaluation_form(const SemanticExpression& source) const noexcept
    -> EvaluationForm {
    const auto folded = source.constant
        && evaluation_rule(context.semantic(), source).action != EvaluationAction::Required
        && !evaluation_preserves_full_expression(context.semantic(), source);
    if (folded && !evaluation_requires_execution(context.semantic(), source)) {
        return EvaluationForm::Expression;
    }
    if (const auto* propagation = std::get_if<SemPropagate>(&source.value)) {
        return evaluation_form(*propagation->operand);
    }
    if (const auto* logic = std::get_if<SemShortCircuit>(&source.value)) {
        return folded
                || evaluation_form(*logic->left) != EvaluationForm::Expression
                || evaluation_form(*logic->right) != EvaluationForm::Expression
                || evaluation_preserves_full_expression(context.semantic(), source)
            ? EvaluationForm::Branches
            : EvaluationForm::Expression;
    }
    if (can_extend_branch_scope(source)) {
        return EvaluationForm::Branches;
    }
    if (std::holds_alternative<SemIf>(source.value)
        || std::holds_alternative<SemMatch>(source.value)
        || std::holds_alternative<SemTry>(source.value)) {
        return folded || context.is_void(source.type.resolved()) || external_exits(source)
            ? EvaluationForm::Statements
            : EvaluationForm::Expression;
    }
    const auto group = operation_operands(source);
    auto children = EvaluationForm::Expression;
    for (const auto& operand : group.values) {
        children = std::max(children, evaluation_form(operand.expression));
    }
    if (children == EvaluationForm::Branches) {
        return children;
    }
    if (const auto* borrow = std::get_if<SemBorrowCallable>(&source.value);
        borrow != nullptr && evaluation_requires_execution(context.semantic(), *borrow->source)) {
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

namespace {
auto copy_operand(const TargetExpr& source) noexcept -> TargetExpr {
    const auto copy_values = [](const std::vector<TargetExpr>& values) static noexcept {
        return values | std::views::transform(copy_operand) | std::ranges::to<std::vector>();
    };
    return std::visit(
        Overloaded {
            [&](const TargetPrefixExpr& value) noexcept -> TargetExpr {
                return {
                    .value = TargetPrefixExpr {
                        .op = value.op,
                        .operand = target_child(copy_operand(*value.operand))
                    }
                };
            },
            [&](const TargetStaticCastExpr& value) noexcept -> TargetExpr {
                return {
                    .value = TargetStaticCastExpr {
                        .type = value.type,
                        .operand = target_child(copy_operand(*value.operand))
                    }
                };
            },
            [&](const TargetCallExpr& value) noexcept -> TargetExpr {
                return {
                    .value = TargetCallExpr {
                        .callee = target_child(copy_operand(*value.callee)),
                        .template_argument_type_ids = value.template_argument_type_ids,
                        .arguments = copy_values(value.arguments)
                    }
                };
            },
            [&](const TargetMemberExpr& value) noexcept -> TargetExpr {
                return {
                    .value = TargetMemberExpr {
                        .operand = target_child(copy_operand(*value.operand)),
                        .name = value.name
                    }
                };
            },
            [&](const TargetConstructionExpr& value) noexcept -> TargetExpr {
                auto initializer = std::visit(
                    Overloaded {
                        [](std::monostate) static -> decltype(value.initializer) {
                            return std::monostate {};
                        },
                        [&](const std::vector<TargetExpr>& values) -> decltype(value.initializer) {
                            return copy_values(values);
                        },
                        [&](const std::vector<TargetFieldInitializer>& values)
                            -> decltype(value.initializer) {
                            auto fields = std::vector<TargetFieldInitializer>();
                            for (const auto& field : values) {
                                fields.push_back(
                                    {.name = field.name,
                                     .value = target_child(copy_operand(*field.value))}
                                );
                            }
                            return fields;
                        }
                    },
                    value.initializer
                );
                return {
                    .value = TargetConstructionExpr {
                        .type = value.type,
                        .initializer = std::move(initializer)
                    }
                };
            },
            [&](const TargetArrayExpr& value) noexcept -> TargetExpr {
                return {
                    .value = TargetArrayExpr {
                        .element_type_id = value.element_type_id,
                        .extent = target_child(copy_operand(*value.extent)),
                        .elements = copy_values(value.elements)
                    }
                };
            },
            [](const auto& value) static noexcept -> TargetExpr {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TargetNameExpr>
                              || std::same_as<Value, TargetIntrinsicNameExpr>
                              || std::same_as<Value, TargetLiteralExpr>
                              || std::same_as<Value, TargetStaticMemberExpr>) {
                    return {.value = value};
                } else {
                    invariant_violation("branch continuation requires a stable operand");
                }
            }
        },
        source.value
    );
}
} // namespace

auto BodyLowerer::consume_operands(
    const OperandGroup& group,
    LoweringLiteralContext literal,
    bool materializing,
    const std::function<void(std::vector<TargetExpr>, LoweringStmtBuilder&)>& consume,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto children = EvaluationForm::Expression;
    for (const auto& operand : group.values) {
        children = std::max(children, evaluation_form(operand.expression));
    }
    const auto repeated = children == EvaluationForm::Branches;
    const auto stabilize = requires_materialization(group, children != EvaluationForm::Expression);
    auto values = std::vector<TargetExpr>();
    const auto next = [&](this const auto& self,
                          std::size_t index,
                          LoweringStmtBuilder& statements) noexcept -> void {
        if (index == group.values.size()) {
            auto arguments = std::vector<TargetExpr>();
            for (auto& value : values) {
                arguments.push_back(repeated ? copy_operand(value) : std::move(value));
            }
            consume(std::move(arguments), statements);
            return;
        }
        const auto& input = group.values[index];
        consume_expression(
            input.expression,
            literal,
            input.use == OperandUse::Read || input.use == OperandUse::ConstPlace
                ? ResultDemand::Observe
                : ResultDemand::Value,
            [&](LoweringResult evaluated, LoweringStmtBuilder& branch) noexcept {
                const auto temporary = std::holds_alternative<LoweringTemporaryValue>(evaluated);
                auto value = require_expression(
                    std::move(evaluated),
                    input.use == OperandUse::Own || input.use == OperandUse::Snapshot
                        ? LoweringResultUse::Transfer
                        : LoweringResultUse::Observe
                );
                if (stabilize && (!temporary || input.use == OperandUse::Own)) {
                    value =
                        materialize_operand(input.expression, std::move(value), input.use, branch);
                } else if (input.use == OperandUse::ConstPlace
                           || (input.use == OperandUse::Read
                               && evaluation_reads_storage(context.semantic(), input.expression))) {
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
                self(index + 1uz, branch);
                values.pop_back();
            },
            statements,
            materializing || stabilize
        );
    };
    next(0uz, destination);
}
