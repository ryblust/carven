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

namespace body_lowering {
namespace {

auto immediate_operand(const SemanticExpression& expression) noexcept -> bool {
    return std::holds_alternative<SemConstant>(expression.value)
        || std::holds_alternative<SemBinding>(expression.value)
        || std::holds_alternative<SemCallable>(expression.value)
        || std::holds_alternative<SemEnumConstructor>(expression.value);
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
        return call_expression(
            intrinsic_expression(*runtime),
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
    return binary_expression(std::move(left), target, std::move(right));
}

auto BodyLowerer::expression(
    const SemanticExpression& source,
    StatementSequence& destination
) noexcept -> std::optional<TargetExpr> {
    if (!destination.continues()) {
        return std::nullopt;
    }
    return std::visit(
        Overloaded {
            [&](const SemCppCall& call) noexcept -> std::optional<TargetExpr> {
                return cpp_call(call, destination);
            },
            [&](const SemCpp& value) noexcept -> std::optional<TargetExpr> {
                return cpp_operation(source, value, destination);
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
                    return TargetExpr {
                        .value = TargetConstructionExpr {
                            .type = context.lower_type(to),
                            .initializer = target_expressions(std::move(input))
                        }
                    };
                };
                auto input = expression(*value.source, destination);
                if (!input) {
                    return std::nullopt;
                }
                return adopt(
                    std::move(*input),
                    value.source->type.resolved(),
                    source.type.resolved()
                );
            },
            [&](const SemArray& value) noexcept -> std::optional<TargetExpr> {
                const auto& array = std::get<ArrayTypeValue>(
                    context.semantic().types().type(source.type.resolved()).value
                );
                auto sources = std::vector<const SemanticExpression*>();
                for (const auto& element : value.elements) {
                    sources.push_back(&element);
                }
                auto elements = initializers(sources, true, destination);
                if (!destination.continues()) {
                    return std::nullopt;
                }
                return TargetExpr {
                    .value = TargetArrayExpr {
                        .element_type_id = context.lower_type(array.element),
                        .extent = target_child(integer_expression(array.extent)),
                        .elements = std::move(*elements)
                    }
                };
            },
            [&](const SemStruct& value) noexcept -> std::optional<TargetExpr> {
                auto sources = std::vector<const SemanticExpression*>();
                for (const auto& field : value.fields) {
                    sources.push_back(&field.value);
                }
                const auto in_order = std::ranges::is_sorted(
                    value.fields,
                    {},
                    &SemFieldInitializer::declaration_index
                );
                auto values = initializers(sources, in_order, destination);
                if (!destination.continues()) {
                    return std::nullopt;
                }
                auto fields = std::vector<std::pair<std::uint32_t, TargetFieldInitializer>>();
                for (auto index = 0uz; index < value.fields.size(); ++index) {
                    const auto& field = value.fields[index];
                    fields.push_back(
                        {field.declaration_index,
                         {.name = field_identifier(value.structure, field.declaration_index),
                          .value = target_child(std::move((*values)[index]))}}
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
                const auto direct = value.payload.size() <= 1uz
                    || std::ranges::all_of(value.payload, immediate_operand);
                auto sources = std::vector<Operand>();
                for (const auto& element : value.payload) {
                    sources.push_back({element, direct ? OperandUse::Direct : OperandUse::Own});
                }
                auto payload = operands(sources, destination);
                if (!payload) {
                    return std::nullopt;
                }
                return enum_case_expression(context, value.enum_case, std::move(*payload));
            },
            [&](const SemUnary& value) noexcept -> std::optional<TargetExpr> {
                auto argument = expression(*value.operand, destination);
                if (!argument) {
                    return std::nullopt;
                }
                if (value.operation == UnaryOperator::Negate
                    && context.is_integer(source.type.resolved())) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeIntegerNegate),
                        target_expressions(std::move(*argument))
                    );
                }
                const auto operation = value.operation == UnaryOperator::LogicalNot
                    ? TargetPrefixOperator::LogicalNot
                    : value.operation == UnaryOperator::Negate ? TargetPrefixOperator::Negate
                                                               : TargetPrefixOperator::BitwiseNot;
                return prefix_expression(operation, std::move(*argument));
            },
            [&](const SemBinary& value) noexcept -> std::optional<TargetExpr> {
                const auto fixed = [](const SemanticExpression& expression) static noexcept {
                    return std::holds_alternative<SemConstant>(expression.value);
                };
                const auto direct =
                    (immediate_operand(*value.left) && immediate_operand(*value.right))
                    || fixed(*value.left)
                    || fixed(*value.right);
                const auto use = direct ? OperandUse::Direct : OperandUse::Snapshot;
                auto arguments = operands(
                    std::array {Operand {*value.left, use}, Operand {*value.right, use}},
                    destination
                );
                if (!arguments) {
                    return std::nullopt;
                }
                return binary(
                    std::move((*arguments)[0]),
                    value.operation,
                    std::move((*arguments)[1]),
                    source.type.resolved()
                );
            },
            [&](const SemShortCircuit& value) noexcept -> std::optional<TargetExpr> {
                auto left = expression(*value.left, destination);
                if (!left) {
                    return std::nullopt;
                }
                if (const auto known = known_boolean(*value.left)) {
                    destination.emit(
                        generated_statement(TargetDiscardStmt {.expression = std::move(*left)})
                    );
                    if ((*known && value.operation == ShortCircuitOperator::Or)
                        || (!*known && value.operation == ShortCircuitOperator::And)) {
                        return bool_expression(*known);
                    }
                    return expression(*value.right, destination);
                }
                auto right_statements = StatementSequence();
                auto right = expression(*value.right, right_statements);
                if (right_statements.empty()) {
                    return binary_expression(
                        std::move(*left),
                        value.operation == ShortCircuitOperator::And
                            ? TargetBinaryOperator::LogicalAnd
                            : TargetBinaryOperator::LogicalOr,
                        std::move(*right)
                    );
                }
                const auto name = names.fresh(TargetTemporaryNameKind::Operand);
                destination.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = name,
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(*left)
                    }
                ));
                if (right_statements.continues()) {
                    right_statements.emit(generated_statement(
                        TargetAssignmentStmt {
                            .target = name_expression(name),
                            .op = TargetAssignmentOperator::Assign,
                            .value = std::move(*right)
                        }
                    ));
                }
                auto condition = name_expression(name);
                if (value.operation == ShortCircuitOperator::Or) {
                    condition =
                        prefix_expression(TargetPrefixOperator::LogicalNot, std::move(condition));
                }
                auto branches = std::vector<TargetIfBranch>();
                branches.push_back(
                    {.condition = std::move(condition),
                     .body = std::move(right_statements).finish()}
                );
                destination.emit(generated_statement(
                    TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                ));
                return name_expression(name);
            },
            [&](const SemCast& value) noexcept -> std::optional<TargetExpr> {
                auto argument = expression(*value.operand, destination);
                if (!argument) {
                    return std::nullopt;
                }
                if (value.kind == CastKind::Identity) {
                    return argument;
                }
                if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                        &context.semantic().types().type(source.type.resolved()).value
                    );
                    builtin != nullptr && builtin->kind == BuiltinType::Char) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
                        target_expressions(std::move(*argument))
                    );
                }
                return TargetExpr {
                    .value = TargetStaticCastExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .operand = target_child(std::move(*argument))
                    }
                };
            },
            [&](const SemField& value) noexcept -> std::optional<TargetExpr> {
                return expression(*value.source, destination)
                    .transform([&](TargetExpr owner) noexcept {
                        return member_expression(
                            std::move(owner),
                            field_identifier(value.field.owner, value.field.field_index)
                        );
                    });
            },
            [&](const SemIndex& value) noexcept -> std::optional<TargetExpr> {
                auto arguments = operands(
                    std::array {
                        Operand {*value.source, OperandUse::Place},
                        Operand {*value.index, OperandUse::Snapshot}
                    },
                    destination
                );
                if (!arguments) {
                    return std::nullopt;
                }
                if (std::holds_alternative<RuntimeCheckedBounds>(value.bounds)) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeCheckedArrayIndex),
                        target_expressions(std::move((*arguments)[0]), std::move((*arguments)[1]))
                    );
                }
                return TargetExpr {
                    .value = TargetIndexExpr {
                        .operand = target_child(std::move((*arguments)[0])),
                        .index = target_child(std::move((*arguments)[1]))
                    }
                };
            },
            [&](const SemTextIntrinsic& value) noexcept -> std::optional<TargetExpr> {
                auto owner = expression(*value.source, destination);
                if (!owner) {
                    return std::nullopt;
                }
                switch (value.intrinsic) {
                    case TextIntrinsic::Len:     return call_member(std::move(*owner), "size", {});
                    case TextIntrinsic::IsEmpty: return call_member(std::move(*owner), "empty", {});
                    case TextIntrinsic::Bytes:
                        return call_expression(
                            intrinsic_expression(TargetSymbol::RuntimeStrBytes),
                            target_expressions(std::move(*owner))
                        );
                    case TextIntrinsic::Chars:
                        return call_expression(
                            intrinsic_expression(TargetSymbol::RuntimeStrChars),
                            target_expressions(std::move(*owner))
                        );
                }
                std::unreachable();
            },
            [&](const SemClosure& value) noexcept -> std::optional<TargetExpr> {
                const auto direct = value.captures.size() <= 1uz
                    || std::ranges::all_of(value.captures, [](const auto& capture) static noexcept {
                                        return immediate_operand(capture.expression);
                                    });
                auto sources = std::vector<Operand>();
                for (const auto& capture : value.captures) {
                    sources.push_back(
                        {capture.expression,
                         direct                                   ? OperandUse::Direct
                             : capture.mode == CaptureMode::Write ? OperandUse::Place
                                                                  : OperandUse::Own}
                    );
                }
                auto captures = operands(sources, destination);
                if (!captures) {
                    return std::nullopt;
                }
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(*captures)
                    }
                };
            },
            [&](const SemBorrowCallable& value) noexcept -> std::optional<TargetExpr> {
                auto backing = immediate_operand(*value.source)
                    ? expression(*value.source, destination)
                    : operand(*value.source, destination, OperandUse::Place);
                if (!backing) {
                    return std::nullopt;
                }
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = target_expressions(std::move(*backing))
                    }
                };
            },
            [&](const SemTake& value) noexcept -> std::optional<TargetExpr> {
                return expression(*value.place, destination)
                    .transform([](TargetExpr place) static noexcept {
                        return transfer_expression(std::move(place));
                    });
            },
            [&](const SemPropagate& value) noexcept -> std::optional<TargetExpr> {
                return expression(*value.operand, destination);
            },
            [&](const SemCall& value) noexcept -> std::optional<TargetExpr> {
                const auto fixed_callee = std::holds_alternative<SemCallable>(value.callee->value)
                    || std::holds_alternative<SemEnumConstructor>(value.callee->value);
                const auto direct = (fixed_callee && value.arguments.size() <= 1uz)
                    || (immediate_operand(*value.callee)
                        && std::ranges::all_of(
                            value.arguments,
                            [](const auto& argument) static noexcept {
                                return argument.access != AccessMode::Take
                                    && immediate_operand(argument.expression);
                            }
                        ));
                const auto closure = std::holds_alternative<ClosureTypeValue>(
                    context.semantic().types().type(value.callee->type.resolved()).value
                );
                auto sources = std::vector<Operand>();
                sources.push_back(
                    {*value.callee,
                     direct        ? OperandUse::Direct
                         : closure ? OperandUse::ConstPlace
                                   : OperandUse::Snapshot}
                );
                for (const auto& argument : value.arguments) {
                    sources.push_back(
                        {argument.expression,
                         direct                                     ? OperandUse::Direct
                             : argument.access == AccessMode::Write ? OperandUse::Place
                             : argument.access == AccessMode::Take  ? OperandUse::Own
                                                                    : OperandUse::Read}
                    );
                }
                auto values = operands(sources, destination);
                if (!values) {
                    return std::nullopt;
                }
                auto callee = std::move(values->front());
                values->erase(values->begin());
                auto call = call_expression(std::move(callee), std::move(*values));
                const auto failures =
                    context.plan().failure_abi().members(value.callee_failures.resolved());
                if (failures.empty()) {
                    return call;
                }
                const auto outcome = names.fresh(TargetTemporaryNameKind::Outcome);
                destination.emit(source_statement(
                    context.semantic(),
                    source.origin,
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = outcome,
                        .type = context.intrinsic_type(TargetSymbol::Auto),
                        .initializer = std::move(call)
                    }
                ));
                const auto success = names.fresh(TargetTemporaryNameKind::SuccessProjection);
                destination.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = success,
                        .type = context.pointer_type(context.intrinsic_type(
                            TargetSymbol::Auto,
                            context.is_void(source.type.resolved())
                        )),
                        .initializer = call_member(name_expression(outcome), "success_if", {})
                    }
                ));
                auto failure_body = StatementSequence();
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
                                    name_expression(outcome),
                                    TargetIdentifier::from_spelling("failure_if")
                                ),
                                {context.lower_type(failure)},
                                {}
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
                        {.condition = name_expression(projection),
                         .body = std::move(transfer).finish()}
                    );
                    failure_body.emit(generated_statement(
                        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                    ));
                }
                failure_body.terminate(generated_statement(
                    TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
                ));
                auto branches = std::vector<TargetIfBranch>();
                branches.push_back(
                    {.condition = prefix_expression(
                         TargetPrefixOperator::LogicalNot,
                         name_expression(success)
                     ),
                     .body = std::move(failure_body).finish()}
                );
                destination.emit(generated_statement(
                    TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                ));
                if (context.is_void(source.type.resolved())) {
                    if (!destination.continues()) {
                        return std::nullopt;
                    }
                    return TargetExpr {
                        .value = TargetConstructionExpr {
                            .type = context.intrinsic_type(TargetSymbol::Void),
                            .initializer = std::monostate {}
                        }
                    };
                }
                return transfer_expression(member_expression(
                    dereference_expression(name_expression(success)),
                    TargetIdentifier::from_spelling("value")
                ));
            },
            [&](const auto&) noexcept -> std::optional<TargetExpr> {
                if (context.is_void(source.type.resolved())) {
                    structured_expression(
                        source,
                        {.use = ResultUse::Discard, .storage = std::nullopt},
                        destination
                    );
                    if (!destination.continues()) {
                        return std::nullopt;
                    }
                    return TargetExpr {
                        .value = TargetConstructionExpr {
                            .type = context.intrinsic_type(TargetSymbol::Void),
                            .initializer = std::monostate {}
                        }
                    };
                }
                if (context.plan().failure_abi().members(source.failures.resolved()).empty()
                    && !source.exits_test) {
                    auto statements = StatementSequence();
                    const auto previous = std::exchange(region_return, false);
                    structured_expression(
                        source,
                        {.use = ResultUse::Return, .storage = std::nullopt},
                        statements
                    );
                    const auto returns = *std::exchange(region_return, previous);
                    auto expression = TargetExpr {
                        .value = TargetRegionExpr {
                            .result = context.lower_type(source.type.resolved()),
                            .body = std::move(statements).finish()
                        }
                    };
                    if (!returns) {
                        destination.terminate(statement_expression(std::move(expression)));
                        return std::nullopt;
                    }
                    return expression;
                }
                const auto storage = names.fresh(TargetTemporaryNameKind::Operand);
                auto statements = StatementSequence();
                structured_expression(
                    source,
                    {.use = ResultUse::Store, .storage = storage},
                    statements
                );
                if (!statements.continues()) {
                    destination.append(std::move(statements));
                    return std::nullopt;
                }
                destination.emit(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = storage,
                        .type = context.optional_type(context.lower_type(source.type.resolved())),
                        .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
                    }
                ));
                destination.append(std::move(statements));
                return transfer_expression(dereference_expression(name_expression(storage)));
            },
        },
        source.value
    );
}

} // namespace body_lowering
