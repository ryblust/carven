module carven:backend.lowering.body.expr.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace {
auto restore_integer_type(ModuleLowering& context, TargetExpr expression, TypeID type) noexcept
    -> TargetExpr {
    const auto* builtin =
        std::get_if<BuiltinTypeValue>(&context.semantic().types().type(type).value);
    if (builtin != nullptr
        && builtin_integer_width(builtin->kind) < std::numeric_limits<unsigned int>::digits) {
        return {
            .value = TargetStaticCastExpr {
                .type = context.lower_type(type),
                .operand = target_child(std::move(expression))
            }
        };
    }
    return expression;
}

auto adapt_callable_view(ModuleLowering& context, TargetExpr input, TypeID from, TypeID to) noexcept
    -> TargetExpr {
    const auto& type = context.semantic().types().type(from);
    if (const auto* closure = std::get_if<ClosureTypeValue>(&type.value)) {
        const auto body_id = context.semantic().declarations().body_for_callable(closure->callable);
        if (context.semantic().bodies().body(*body_id).inputs().captures.empty()) {
            return call_expression(
                static_member_expression(
                    context.lower_type(to),
                    TargetIdentifier::from_spelling("from_stateless")
                ),
                target_expressions(std::move(input))
            );
        }
    }
    return TargetExpr {
        .value = TargetConstructionExpr {
            .type = context.lower_type(to),
            .initializer = target_expressions(std::move(input))
        }
    };
}
} // namespace

auto BodyLowerer::binary(
    TargetExpr left,
    BinaryOperator operation,
    TargetExpr right,
    TypeID type
) noexcept -> TargetExpr {
    auto runtime = std::optional<TargetSymbol>();
    if (context.is_integer(type)) {
        switch (operation) {
            case BinaryOperator::Add:       runtime = TargetSymbol::RuntimeIntegerAdd; break;
            case BinaryOperator::Subtract:  runtime = TargetSymbol::RuntimeIntegerSubtract; break;
            case BinaryOperator::Multiply:  runtime = TargetSymbol::RuntimeIntegerMultiply; break;
            case BinaryOperator::Divide:    runtime = TargetSymbol::RuntimeIntegerDivide; break;
            case BinaryOperator::Remainder: runtime = TargetSymbol::RuntimeIntegerRemainder; break;
            case BinaryOperator::LeftShift: runtime = TargetSymbol::RuntimeIntegerLeftShift; break;
            case BinaryOperator::RightShift:
                runtime = TargetSymbol::RuntimeIntegerRightShift;
                break;
            default: break;
        }
    }
    if (runtime.has_value()) {
        return template_call_expression(
            intrinsic_expression(*runtime),
            {context.lower_type(type)},
            target_expressions(std::move(left), std::move(right))
        );
    }
    const auto target = [&]() noexcept {
        switch (operation) {
            case BinaryOperator::BitwiseOr:    return TargetBinaryOperator::BitwiseOr;
            case BinaryOperator::BitwiseXor:   return TargetBinaryOperator::BitwiseXor;
            case BinaryOperator::BitwiseAnd:   return TargetBinaryOperator::BitwiseAnd;
            case BinaryOperator::Equal:        return TargetBinaryOperator::Equal;
            case BinaryOperator::NotEqual:     return TargetBinaryOperator::NotEqual;
            case BinaryOperator::Less:         return TargetBinaryOperator::Less;
            case BinaryOperator::LessEqual:    return TargetBinaryOperator::LessEqual;
            case BinaryOperator::Greater:      return TargetBinaryOperator::Greater;
            case BinaryOperator::GreaterEqual: return TargetBinaryOperator::GreaterEqual;
            case BinaryOperator::LeftShift:    return TargetBinaryOperator::LeftShift;
            case BinaryOperator::RightShift:   return TargetBinaryOperator::RightShift;
            case BinaryOperator::Add:          return TargetBinaryOperator::Add;
            case BinaryOperator::Subtract:     return TargetBinaryOperator::Subtract;
            case BinaryOperator::Multiply:     return TargetBinaryOperator::Multiply;
            case BinaryOperator::Divide:       return TargetBinaryOperator::Divide;
            case BinaryOperator::Remainder:    return TargetBinaryOperator::Remainder;
        }
        std::unreachable();
    }();
    auto result = binary_expression(std::move(left), target, std::move(right));
    if (context.is_integer(type)
        && (operation == BinaryOperator::BitwiseAnd
            || operation == BinaryOperator::BitwiseOr
            || operation == BinaryOperator::BitwiseXor)) {
        return restore_integer_type(context, std::move(result), type);
    }
    return result;
}

auto BodyLowerer::construct_operation(
    const SemanticExpression& source,
    std::vector<TargetExpr> operands,
    ResultDemand demand
) noexcept -> Lowered<LoweringResult> {
    auto destination = LoweringStmtBuilder();
    auto owned_result = false;
    auto result = std::visit(
        Overloaded {
            [&](const SemCppCall& call) noexcept -> std::optional<TargetExpr> {
                return cpp_call(call, std::move(operands));
            },
            [&](const SemCpp& value) noexcept -> std::optional<TargetExpr> {
                return cpp_operation(source, value, std::move(operands));
            },
            [&](const SemConstant& value) noexcept -> std::optional<TargetExpr> {
                return constant_expression(context, value.constant);
            },
            [&](const SemBinding& value) noexcept -> std::optional<TargetExpr> {
                return binding_expression(value.binding);
            },
            [&](const SemCallable& value) noexcept -> std::optional<TargetExpr> {
                return name_expression(context.callable_name(value.callable));
            },
            [&](const SemEnumConstructor& value) noexcept -> std::optional<TargetExpr> {
                const auto owner =
                    context.semantic().declarations().enum_case(value.enum_case).owner;
                return static_member_expression(
                    context.named_type(context.enumeration_name(owner)),
                    context.names().enum_case_identifier(value.enum_case)
                );
            },
            [&](const SemArrayAdopt& value) noexcept -> std::optional<TargetExpr> {
                const auto adopt = [&](this const auto& self,
                                       TargetExpr input,
                                       TypeID from,
                                       TypeID to) noexcept -> TargetExpr {
                    if (from == to) {
                        return input;
                    }
                    if (const auto* target = std::get_if<ArrayTypeValue>(
                            &context.semantic().types().type(to).value
                        )) {
                        const auto& source_array =
                            std::get<ArrayTypeValue>(context.semantic().types().type(from).value);
                        const auto owner = names.fresh(TargetTemporaryNameKind::Owner);
                        destination.emit(generated_statement(
                            TargetVariableStmt {
                                .binding = TargetVariableBinding::RvalueReference,
                                .maybe_unused = false,
                                .name = owner,
                                .type = context.intrinsic_type(TargetSymbol::Auto),
                                .initializer = std::move(input)
                            }
                        ));
                        auto elements = std::vector<TargetExpr>();
                        for (auto index = 0uz; index < target->extent; ++index) {
                            elements.push_back(self(
                                TargetExpr {
                                    .value =
                                        TargetIndexExpr {
                                            .operand = target_child(name_expression(owner)),
                                            .index = target_child(integer_expression(index))
                                        }
                                },
                                source_array.element,
                                target->element
                            ));
                        }
                        return TargetExpr {
                            .value = TargetArrayExpr {
                                .element_type_id = context.lower_type(target->element),
                                .extent = target_child(integer_expression(target->extent)),
                                .elements = std::move(elements)
                            }
                        };
                    }
                    return adapt_callable_view(context, std::move(input), from, to);
                };
                return adopt(
                    std::move(operands[0]),
                    value.source->type.resolved(),
                    source.type.resolved()
                );
            },
            [&](const SemArray&) noexcept -> std::optional<TargetExpr> {
                const auto& array = std::get<ArrayTypeValue>(
                    context.semantic().types().type(source.type.resolved()).value
                );
                return TargetExpr {
                    .value = TargetArrayExpr {
                        .element_type_id = context.lower_type(array.element),
                        .extent = target_child(integer_expression(array.extent)),
                        .elements = std::move(operands)
                    }
                };
            },
            [&](const SemStruct& value) noexcept -> std::optional<TargetExpr> {
                auto fields = std::vector<std::pair<std::uint32_t, TargetFieldInitializer>>();
                for (auto index = 0uz; index < value.fields.size(); ++index) {
                    const auto& field = value.fields[index];
                    fields.push_back(
                        {field.declaration_index,
                         {.name = field_identifier(value.structure, field.declaration_index),
                          .value = target_child(std::move(operands[index]))}}
                    );
                }
                std::ranges::sort(fields, {}, [](const auto& field) static noexcept {
                    return field.first;
                });
                auto ordered = std::vector<TargetFieldInitializer>();
                for (auto& [index, field] : fields) {
                    static_cast<void>(index);
                    ordered.push_back(std::move(field));
                }
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(ordered)
                    }
                };
            },
            [&](const SemEnumCase& value) noexcept -> std::optional<TargetExpr> {
                return enum_case_expression(context, value.enum_case, std::move(operands));
            },
            [&](const SemUnary& value) noexcept -> std::optional<TargetExpr> {
                if (value.operation == UnaryOperator::Negate
                    && context.is_integer(source.type.resolved())) {
                    return template_call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeIntegerNegate),
                        {context.lower_type(source.type.resolved())},
                        target_expressions(std::move(operands[0]))
                    );
                }
                const auto operation = value.operation == UnaryOperator::LogicalNot
                    ? TargetPrefixOperator::LogicalNot
                    : value.operation == UnaryOperator::Negate ? TargetPrefixOperator::Negate
                                                               : TargetPrefixOperator::BitwiseNot;
                auto result = prefix_expression(operation, std::move(operands[0]));
                if (value.operation == UnaryOperator::BitwiseNot) {
                    return restore_integer_type(context, std::move(result), source.type.resolved());
                }
                return result;
            },
            [&](const SemBinary& value) noexcept -> std::optional<TargetExpr> {
                return binary(
                    std::move(operands[0]),
                    value.operation,
                    std::move(operands[1]),
                    source.type.resolved()
                );
            },
            [](const SemShortCircuit&) static noexcept -> std::optional<TargetExpr> {
                invariant_violation("short circuit is composed by evaluation");
            },
            [&](const SemCast& value) noexcept -> std::optional<TargetExpr> {
                if (value.kind == CastKind::Identity) {
                    return std::move(operands[0]);
                }
                if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                        &context.semantic().types().type(source.type.resolved()).value
                    );
                    builtin != nullptr && builtin->kind == BuiltinType::Char) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
                        target_expressions(std::move(operands[0]))
                    );
                }
                return TargetExpr {
                    .value = TargetStaticCastExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .operand = target_child(std::move(operands[0]))
                    }
                };
            },
            [&](const SemDereference&) noexcept -> std::optional<TargetExpr> {
                return dereference_expression(std::move(operands[0]));
            },
            [&](const SemField& value) noexcept -> std::optional<TargetExpr> {
                return member_expression(
                    std::move(operands[0]),
                    field_identifier(value.field.owner, value.field.field_index)
                );
            },
            [&](const SemIndex& value) noexcept -> std::optional<TargetExpr> {
                if (std::holds_alternative<RuntimeCheckedBounds>(value.bounds)) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeCheckedArrayIndex),
                        target_expressions(std::move(operands[0]), std::move(operands[1]))
                    );
                }
                return TargetExpr {
                    .value = TargetIndexExpr {
                        .operand = target_child(std::move(operands[0])),
                        .index = target_child(std::move(operands[1]))
                    }
                };
            },
            [&](const SemTextIntrinsic& value) noexcept -> std::optional<TargetExpr> {
                switch (value.intrinsic) {
                    case TextIntrinsic::Len: return call_member(std::move(operands[0]), "size", {});
                    case TextIntrinsic::IsEmpty:
                        return call_member(std::move(operands[0]), "empty", {});
                    case TextIntrinsic::Bytes:
                        return call_expression(
                            intrinsic_expression(TargetSymbol::RuntimeStrBytes),
                            target_expressions(std::move(operands[0]))
                        );
                    case TextIntrinsic::Chars:
                        return call_expression(
                            intrinsic_expression(TargetSymbol::RuntimeStrChars),
                            target_expressions(std::move(operands[0]))
                        );
                }
                std::unreachable();
            },
            [&](const SemClosure&) noexcept -> std::optional<TargetExpr> {
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(operands)
                    }
                };
            },
            [&](const SemBorrowCallable& value) noexcept -> std::optional<TargetExpr> {
                return adapt_callable_view(
                    context,
                    std::move(operands.front()),
                    value.source->type.resolved(),
                    source.type.resolved()
                );
            },
            [&](const SemTake&) noexcept -> std::optional<TargetExpr> {
                owned_result = true;
                return std::move(operands[0]);
            },
            [](const SemPropagate&) static noexcept -> std::optional<TargetExpr> {
                invariant_violation("propagation is forwarded by evaluation");
            },
            [&](const SemCall& value) noexcept -> std::optional<TargetExpr> {
                auto callee = std::move(operands.front());
                operands.erase(operands.begin());
                auto call = call_expression(std::move(callee), std::move(operands));
                const auto failures =
                    context.plan().failure_abi().members(value.callee_failures.resolved());
                if (failures.empty()) {
                    return call;
                }
                const auto signature = std::visit(
                    Overloaded {
                        [&](const FunctionTypeValue& type) noexcept {
                            return context.semantic()
                                .declarations()
                                .callable(type.callable)
                                .signature;
                        },
                        [&](const ClosureTypeValue& type) noexcept {
                            return context.semantic()
                                .declarations()
                                .callable(type.callable)
                                .signature;
                        },
                        [](const CallableViewTypeValue& type) static noexcept {
                            return type.signature;
                        },
                        [](const auto&) static noexcept -> CallableSignatureID {
                            invariant_violation("failing call has no signature");
                        }
                    },
                    context.semantic().types().type(value.callee->type.resolved()).value
                );
                const auto outcome = names.fresh(TargetTemporaryNameKind::Outcome);
                static_cast<void>(materialize_temporary(
                    {outcome, context.outcome_type(signature)},
                    std::move(call),
                    destination
                ));
                const auto outcome_expression = [&]() noexcept {
                    return conditional_temporaries
                        ? dereference_expression(name_expression(outcome))
                        : name_expression(outcome);
                };
                const auto needs_value =
                    demand != ResultDemand::Discard && !context.is_void(source.type.resolved());
                auto success_test = call_member(outcome_expression(), "success_if", {});
                auto success_value = std::optional<TargetExpr>();
                if (needs_value) {
                    const auto success = names.fresh(TargetTemporaryNameKind::SuccessProjection);
                    destination.emit(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = false,
                            .name = success,
                            .type = context.pointer_type(context.intrinsic_type(
                                TargetSymbol::Auto,
                                demand == ResultDemand::Observe
                            )),
                            .initializer = std::move(success_test)
                        }
                    ));
                    success_test = name_expression(success);
                    success_value = member_expression(
                        dereference_expression(name_expression(success)),
                        TargetIdentifier::from_spelling("value")
                    );
                }
                auto failure_body = LoweringStmtBuilder();
                for (const auto failure : failures) {
                    const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
                    failure_body.emit(generated_statement(
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = false,
                            .name = projection,
                            .type =
                                context.pointer_type(context.intrinsic_type(TargetSymbol::Auto)),
                            .initializer = template_call_expression(
                                member_expression(
                                    outcome_expression(),
                                    TargetIdentifier::from_spelling("failure_if")
                                ),
                                {context.lower_type(failure)},
                                {}
                            )
                        }
                    ));
                    auto transfer = LoweringStmtBuilder();
                    emit_failure(
                        transfer_expression(dereference_expression(name_expression(projection))),
                        transfer
                    );
                    failure_body.record_exits(transfer.exits());
                    auto branches = std::vector<TargetIfBranch>();
                    branches.push_back(
                        {.condition = name_expression(projection),
                         .body = std::move(transfer).finish()}
                    );
                    failure_body.emit(generated_statement(
                        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                    ));
                }
                failure_body.terminate(
                    generated_statement(
                        TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
                    ),
                    LoweringExitTarget {LoweringExitKind::Unreachable, 0}
                );
                destination.record_exits(failure_body.exits());
                auto branches = std::vector<TargetIfBranch>();
                branches.push_back(
                    {.condition = prefix_expression(
                         TargetPrefixOperator::LogicalNot,
                         std::move(success_test)
                     ),
                     .body = std::move(failure_body).finish()}
                );
                destination.emit(generated_statement(
                    TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                ));
                owned_result = needs_value;
                return success_value;
            },
            [](const auto&) static noexcept -> std::optional<TargetExpr> {
                invariant_violation("structured operation is composed by evaluation");
            },
        },
        source.value
    );
    if (!destination.continues()) {
        return std::move(destination).complete<LoweringResult>(std::nullopt);
    }
    if (!result) {
        return std::move(destination).complete<LoweringResult>(LoweringCompleted {});
    }
    auto value = owned_result
        ? (std::holds_alternative<SemTake>(source.value)
               ? LoweringResult(LoweringOwnedValue {.storage = std::move(*result)})
               : LoweringResult(LoweringTemporaryValue {.storage = std::move(*result)}))
        : LoweringResult(LoweringDirectExpression {.expression = std::move(*result)});
    return std::move(destination).complete<LoweringResult>(std::move(value));
}
