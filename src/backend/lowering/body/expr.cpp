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

auto immediate_operand(const SemIRExpression& expression) noexcept -> bool {
    return std::holds_alternative<SemLiteral>(expression.value)
        || std::holds_alternative<SemConstant>(expression.value)
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
    const SemIRExpression& source,
    std::vector<TargetStmt>& destination
) noexcept -> TargetExpr {
    const auto no_value = [&]() noexcept -> TargetExpr {
        return {
            .value = TargetConstructionExpr {
                .type = context.intrinsic_type(TargetSymbol::Void),
                .initializer = std::monostate {}
            }
        };
    };
    if (!falls_through(destination)) {
        return no_value();
    }
    return std::visit(
        Overloaded {
            [&](const SemSequence<TypeID, FailureSetID>&) noexcept -> TargetExpr {
                result_expression(
                    source,
                    {.use = ResultUse::Discard, .storage = std::nullopt},
                    destination
                );
                return {
                    .value = TargetConstructionExpr {
                        .type = context.intrinsic_type(TargetSymbol::Void),
                        .initializer = std::monostate {}
                    }
                };
            },
            [&](const SemCpp<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                if (const auto* name = std::get_if<CppNameOperation>(&value.operation)) {
                    return name_expression(context.cpp_name(name->name));
                }
                if (std::holds_alternative<CppConvertOperation>(value.operation)
                    && source.category == SemanticValueCategory::Place) {
                    return expression(value.operands.front().expression, destination);
                }
                if (std::holds_alternative<CppCallOperation>(value.operation)) {
                    const auto& callee_source = value.operands.front().expression;
                    const auto* foreign_callee =
                        std::get_if<SemCpp<TypeID, FailureSetID>>(&callee_source.value);
                    const auto named_callee = foreign_callee != nullptr
                        && (std::holds_alternative<CppNameOperation>(foreign_callee->operation)
                            || std::holds_alternative<CppMemberOperation>(
                                foreign_callee->operation
                            ));
                    auto callee = named_callee
                        ? expression(callee_source, destination)
                        : operand(callee_source, destination, OperandUse::ConstPlace);
                    auto arguments = std::vector<TargetExpr>();
                    for (const auto& argument : std::span(value.operands).subspan(1)) {
                        arguments.push_back(operand(
                            argument.expression,
                            destination,
                            argument.access == AccessMode::Write      ? OperandUse::Place
                                : argument.access == AccessMode::Take ? OperandUse::Own
                                                                      : OperandUse::Read
                        ));
                    }
                    return call_expression(std::move(callee), std::move(arguments));
                }
                auto arguments = std::vector<TargetExpr>();
                for (const auto& argument : value.operands) {
                    arguments.push_back(operand(
                        argument.expression,
                        destination,
                        (arguments.empty()
                         && (std::holds_alternative<CppMemberOperation>(value.operation)
                             || std::holds_alternative<CppIndexOperation>(value.operation)))
                            ? (argument.access == AccessMode::Write ? OperandUse::Place
                                                                    : OperandUse::ConstPlace)
                            : argument.access == AccessMode::Write ? OperandUse::Place
                            : argument.access == AccessMode::Take  ? OperandUse::Own
                                                                   : OperandUse::Read
                    ));
                }
                if (const auto* operation = std::get_if<CppBinaryOperation>(&value.operation)) {
                    return binary(
                        std::move(arguments[0]),
                        operation->operation,
                        std::move(arguments[1]),
                        source.type
                    );
                }
                if (const auto* update = std::get_if<CppUpdateOperation>(&value.operation)) {
                    return prefix_expression(
                        update->increment ? TargetPrefixOperator::Increment
                                          : TargetPrefixOperator::Decrement,
                        std::move(arguments.front())
                    );
                }
                if (const auto* operation = std::get_if<CppUnaryOperation>(&value.operation)) {
                    const auto prefix = operation->operation == UnaryOperator::LogicalNot
                        ? TargetPrefixOperator::LogicalNot
                        : operation->operation == UnaryOperator::Negate
                        ? TargetPrefixOperator::Negate
                        : TargetPrefixOperator::BitwiseNot;
                    return prefix_expression(prefix, std::move(arguments[0]));
                }
                if (const auto* member = std::get_if<CppMemberOperation>(&value.operation)) {
                    return member_expression(
                        std::move(arguments.front()),
                        TargetIdentifier::from_spelling(member->name)
                    );
                }
                if (std::holds_alternative<CppIndexOperation>(value.operation)) {
                    return TargetExpr {
                        .value = TargetIndexExpr {
                            .operand = UniqueIndirect(std::move(arguments[0])),
                            .index = UniqueIndirect(std::move(arguments[1]))
                        }
                    };
                }
                if (const auto* conversion = std::get_if<CppConvertOperation>(&value.operation);
                    conversion != nullptr && conversion->explicit_cast) {
                    return TargetExpr {
                        .value = TargetStaticCastExpr {
                            .type = context.lower_type(source.type),
                            .operand = UniqueIndirect(std::move(arguments.front()))
                        }
                    };
                }
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type),
                        .initializer = std::move(arguments)
                    }
                };
            },
            [&](const SemLiteral& value) noexcept {
                return literal_expression(context, value.value, source.type);
            },
            [&](const SemConstant& value) noexcept {
                return constant_expression(context, value.constant);
            },
            [&](const SemBinding& value) noexcept { return binding_expression(value.binding); },
            [&](const SemCallable& value) noexcept {
                return name_expression(context.callable_name(value.callable));
            },
            [&](const SemEnumConstructor& value) noexcept {
                const auto owner =
                    context.semantic().declarations().enum_case(value.enum_case).owner;
                return static_member_expression(
                    context.named_type(context.enumeration_name(owner)),
                    context.names().enum_case_identifier(value.enum_case)
                );
            },
            [&](const SemArrayAdopt<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
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
                        destination.push_back(generated_statement(
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
                        return {
                            .value = TargetArrayExpr {
                                .element_type_id = context.lower_type(target->element),
                                .extent = target_child(integer_expression(target->extent)),
                                .elements = std::move(elements)
                            }
                        };
                    }
                    return {
                        .value = TargetConstructionExpr {
                            .type = context.lower_type(to),
                            .initializer = target_expressions(std::move(input))
                        }
                    };
                };
                auto input = expression(*value.source, destination);
                if (!falls_through(destination)) {
                    return no_value();
                }
                return adopt(std::move(input), value.source->type, source.type);
            },
            [&](const SemArray<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                const auto& array =
                    std::get<ArrayTypeValue>(context.semantic().types().type(source.type).value);
                auto sources = std::vector<const SemIRExpression*>();
                for (const auto& element : value.elements) {
                    sources.push_back(&element);
                }
                auto elements = initializers(sources, true, destination);
                if (!falls_through(destination)) {
                    return no_value();
                }
                return {
                    .value = TargetArrayExpr {
                        .element_type_id = context.lower_type(array.element),
                        .extent = target_child(integer_expression(array.extent)),
                        .elements = std::move(elements)
                    }
                };
            },
            [&](const SemStruct<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                auto sources = std::vector<const SemIRExpression*>();
                for (const auto& field : value.fields) {
                    sources.push_back(&field.value);
                }
                const auto in_order = std::ranges::is_sorted(
                    value.fields,
                    {},
                    &SemFieldInitializer<TypeID, FailureSetID>::declaration_index
                );
                auto values = initializers(sources, in_order, destination);
                if (!falls_through(destination)) {
                    return no_value();
                }
                auto fields = std::vector<std::pair<std::uint32_t, TargetFieldInitializer>>();
                for (auto index = 0uz; index < value.fields.size(); ++index) {
                    const auto& field = value.fields[index];
                    fields.push_back(
                        {field.declaration_index,
                         {.name = field_identifier(value.structure, field.declaration_index),
                          .value = target_child(std::move(values[index]))}}
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
                return {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type),
                        .initializer = std::move(ordered)
                    }
                };
            },
            [&](const SemEnumCase<TypeID, FailureSetID>& value) noexcept {
                const auto direct = value.payload.size() <= 1uz
                    || std::ranges::all_of(value.payload, immediate_operand);
                auto payload = std::vector<TargetExpr>();
                for (const auto& element : value.payload) {
                    payload.push_back(
                        direct ? expression(element, destination)
                               : operand(element, destination, OperandUse::Own)
                    );
                }
                return enum_case_expression(context, value.enum_case, std::move(payload));
            },
            [&](const SemUnary<TypeID, FailureSetID>& value) noexcept {
                auto argument = expression(*value.operand, destination);
                if (value.operation == UnaryOperator::Negate && context.is_integer(source.type)) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeIntegerNegate),
                        target_expressions(std::move(argument))
                    );
                }
                const auto operation = value.operation == UnaryOperator::LogicalNot
                    ? TargetPrefixOperator::LogicalNot
                    : value.operation == UnaryOperator::Negate ? TargetPrefixOperator::Negate
                                                               : TargetPrefixOperator::BitwiseNot;
                return prefix_expression(operation, std::move(argument));
            },
            [&](const SemBinary<TypeID, FailureSetID>& value) noexcept {
                const auto fixed = [](const SemIRExpression& expression) static noexcept {
                    return std::holds_alternative<SemLiteral>(expression.value)
                        || std::holds_alternative<SemConstant>(expression.value);
                };
                const auto direct =
                    (immediate_operand(*value.left) && immediate_operand(*value.right))
                    || fixed(*value.left)
                    || fixed(*value.right);
                auto left = direct ? expression(*value.left, destination)
                                   : operand(*value.left, destination, OperandUse::Snapshot);
                auto right = direct ? expression(*value.right, destination)
                                    : operand(*value.right, destination, OperandUse::Snapshot);
                return binary(std::move(left), value.operation, std::move(right), source.type);
            },
            [&](const SemShortCircuit<TypeID, FailureSetID>& value) noexcept {
                auto left = expression(*value.left, destination);
                if (!falls_through(destination)) {
                    return no_value();
                }
                if (const auto known = known_boolean(*value.left)) {
                    destination.push_back(
                        generated_statement(TargetDiscardStmt {.expression = std::move(left)})
                    );
                    if ((*known && value.operation == ShortCircuitOperator::Or)
                        || (!*known && value.operation == ShortCircuitOperator::And)) {
                        return bool_expression(*known);
                    }
                    return expression(*value.right, destination);
                }
                auto right_statements = std::vector<TargetStmt>();
                auto right = expression(*value.right, right_statements);
                if (right_statements.empty()) {
                    return binary_expression(
                        std::move(left),
                        value.operation == ShortCircuitOperator::And
                            ? TargetBinaryOperator::LogicalAnd
                            : TargetBinaryOperator::LogicalOr,
                        std::move(right)
                    );
                }
                const auto name = names.fresh(TargetTemporaryNameKind::Operand);
                destination.push_back(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = name,
                        .type = context.lower_type(source.type),
                        .initializer = std::move(left)
                    }
                ));
                if (falls_through(right_statements)) {
                    right_statements.push_back(generated_statement(
                        TargetAssignmentStmt {
                            .target = name_expression(name),
                            .op = TargetAssignmentOperator::Assign,
                            .value = std::move(right)
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
                    {.condition = std::move(condition), .body = std::move(right_statements)}
                );
                destination.push_back(generated_statement(
                    TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                ));
                return name_expression(name);
            },
            [&](const SemCast<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                auto argument = expression(*value.operand, destination);
                if (value.kind == CastKind::Identity) {
                    return argument;
                }
                if (const auto* builtin = std::get_if<BuiltinTypeValue>(
                        &context.semantic().types().type(source.type).value
                    );
                    builtin != nullptr && builtin->kind == BuiltinType::Char) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
                        target_expressions(std::move(argument))
                    );
                }
                return {
                    .value = TargetStaticCastExpr {
                        .type = context.lower_type(source.type),
                        .operand = target_child(std::move(argument))
                    }
                };
            },
            [&](const SemField<TypeID, FailureSetID>& value) noexcept {
                return member_expression(
                    expression(*value.source, destination),
                    field_identifier(value.field.owner, value.field.field_index)
                );
            },
            [&](const SemIndex<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                auto owner = operand(*value.source, destination, OperandUse::Place);
                auto index = operand(*value.index, destination, OperandUse::Snapshot);
                if (std::holds_alternative<RuntimeCheckedBounds>(value.bounds)) {
                    return call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeCheckedArrayIndex),
                        target_expressions(std::move(owner), std::move(index))
                    );
                }
                return {
                    .value = TargetIndexExpr {
                        .operand = target_child(std::move(owner)),
                        .index = target_child(std::move(index))
                    }
                };
            },
            [&](const SemTextIntrinsic<TypeID, FailureSetID>& value) noexcept {
                auto owner = expression(*value.source, destination);
                switch (value.intrinsic) {
                    case TextIntrinsic::Len:     return call_member(std::move(owner), "size", {});
                    case TextIntrinsic::IsEmpty: return call_member(std::move(owner), "empty", {});
                    case TextIntrinsic::Bytes:
                        return call_expression(
                            intrinsic_expression(TargetSymbol::RuntimeStrBytes),
                            target_expressions(std::move(owner))
                        );
                    case TextIntrinsic::Chars:
                        return call_expression(
                            intrinsic_expression(TargetSymbol::RuntimeStrChars),
                            target_expressions(std::move(owner))
                        );
                }
                std::unreachable();
            },
            [&](const SemClosure<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                const auto direct = value.captures.size() <= 1uz
                    || std::ranges::all_of(value.captures, [](const auto& capture) static noexcept {
                                        return immediate_operand(capture.expression);
                                    });
                auto captures = std::vector<TargetExpr>();
                for (const auto& capture : value.captures) {
                    captures.push_back(
                        direct ? expression(capture.expression, destination)
                               : operand(
                                     capture.expression,
                                     destination,
                                     capture.mode == CaptureMode::Write ? OperandUse::Place
                                                                        : OperandUse::Own
                                 )
                    );
                }
                return {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type),
                        .initializer = std::move(captures)
                    }
                };
            },
            [&](const SemBorrowCallable<TypeID, FailureSetID>& value) noexcept -> TargetExpr {
                auto backing = immediate_operand(*value.source)
                    ? expression(*value.source, destination)
                    : operand(*value.source, destination, OperandUse::Place);
                return {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type),
                        .initializer = target_expressions(std::move(backing))
                    }
                };
            },
            [&](const SemTake<TypeID, FailureSetID>& value) noexcept {
                return transfer_expression(expression(*value.place, destination));
            },
            [&](const SemPropagate<TypeID, FailureSetID>& value) noexcept {
                return expression(*value.operand, destination);
            },
            [&](const SemCall<TypeID, FailureSetID>& value) noexcept {
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
                    context.semantic().types().type(value.callee->type).value
                );
                auto callee = direct ? expression(*value.callee, destination)
                                     : operand(
                                           *value.callee,
                                           destination,
                                           closure ? OperandUse::ConstPlace : OperandUse::Snapshot
                                       );
                auto arguments = std::vector<TargetExpr>();
                for (const auto& argument : value.arguments) {
                    arguments.push_back(
                        direct ? expression(argument.expression, destination)
                               : operand(
                                     argument.expression,
                                     destination,
                                     argument.access == AccessMode::Write      ? OperandUse::Place
                                         : argument.access == AccessMode::Take ? OperandUse::Own
                                                                               : OperandUse::Read
                                 )
                    );
                }
                if (!falls_through(destination)) {
                    return no_value();
                }
                auto call = call_expression(std::move(callee), std::move(arguments));
                const auto failures = context.plan().failure_abi().members(value.callee_failures);
                if (failures.empty()) {
                    return call;
                }
                const auto outcome = names.fresh(TargetTemporaryNameKind::Outcome);
                destination.push_back(source_statement(
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
                destination.push_back(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = success,
                        .type = context.pointer_type(
                            context.intrinsic_type(TargetSymbol::Auto, context.is_void(source.type))
                        ),
                        .initializer = call_member(name_expression(outcome), "success_if", {})
                    }
                ));
                auto failure_body = std::vector<TargetStmt>();
                for (const auto failure : failures) {
                    const auto projection = names.fresh(TargetTemporaryNameKind::FailureProjection);
                    failure_body.push_back(generated_statement(
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
                    auto transfer = std::vector<TargetStmt>();
                    emit_failure(
                        transfer_expression(dereference_expression(name_expression(projection))),
                        transfer
                    );
                    auto branches = std::vector<TargetIfBranch>();
                    branches.push_back(
                        {.condition = name_expression(projection), .body = std::move(transfer)}
                    );
                    failure_body.push_back(generated_statement(
                        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                    ));
                }
                failure_body.push_back(generated_statement(
                    TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}
                ));
                auto branches = std::vector<TargetIfBranch>();
                branches.push_back(
                    {.condition = prefix_expression(
                         TargetPrefixOperator::LogicalNot,
                         name_expression(success)
                     ),
                     .body = std::move(failure_body)}
                );
                destination.push_back(generated_statement(
                    TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
                ));
                if (context.is_void(source.type)) {
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
            [&](const auto&) noexcept -> TargetExpr {
                if (context.is_void(source.type)) {
                    structured_expression(
                        source,
                        {.use = ResultUse::Discard, .storage = std::nullopt},
                        destination
                    );
                    return {
                        .value = TargetConstructionExpr {
                            .type = context.intrinsic_type(TargetSymbol::Void),
                            .initializer = std::monostate {}
                        }
                    };
                }
                if (context.plan().failure_abi().members(source.failures).empty()
                    && !source.exits_test) {
                    auto statements = std::vector<TargetStmt>();
                    const auto previous = returning_region;
                    returning_region = true;
                    structured_expression(
                        source,
                        {.use = ResultUse::Return, .storage = std::nullopt},
                        statements
                    );
                    returning_region = previous;
                    mark_unused(statements);
                    return {
                        .value = TargetRegionExpr {
                            .result = context.lower_type(source.type),
                            .body = std::move(statements)
                        }
                    };
                }
                const auto storage = names.fresh(TargetTemporaryNameKind::Operand);
                auto statements = std::vector<TargetStmt>();
                structured_expression(
                    source,
                    {.use = ResultUse::Store, .storage = storage},
                    statements
                );
                if (!falls_through(statements)) {
                    destination.insert(
                        destination.end(),
                        std::make_move_iterator(statements.begin()),
                        std::make_move_iterator(statements.end())
                    );
                    return no_value();
                }
                destination.push_back(generated_statement(
                    TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .name = storage,
                        .type = context.optional_type(context.lower_type(source.type)),
                        .initializer = intrinsic_expression(TargetSymbol::StdNullopt)
                    }
                ));
                destination.insert(
                    destination.end(),
                    std::make_move_iterator(statements.begin()),
                    std::make_move_iterator(statements.end())
                );
                return transfer_expression(dereference_expression(name_expression(storage)));
            },
        },
        source.value
    );
}

} // namespace body_lowering
