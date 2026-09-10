module carven:backend.realization.operation.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.realization.constant;
import :backend.realization.operation;
import :backend.target.expr;
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

} // namespace

auto realize_callable_adaptation(
    ModuleLowering& context,
    TargetExpr input,
    TypeID from,
    TypeID to
) noexcept -> TargetExpr {
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

auto realize_unary(
    ModuleLowering& context,
    UnaryOperator operation,
    TargetExpr operand,
    TypeID type
) noexcept -> TargetExpr {
    if (operation == UnaryOperator::Negate && context.is_integer(type)) {
        return template_call_expression(
            intrinsic_expression(TargetSymbol::RuntimeIntegerNegate),
            {context.lower_type(type)},
            target_expressions(std::move(operand))
        );
    }
    const auto prefix = operation == UnaryOperator::LogicalNot ? TargetPrefixOperator::LogicalNot
        : operation == UnaryOperator::Negate                   ? TargetPrefixOperator::Negate
                                                               : TargetPrefixOperator::BitwiseNot;
    auto result = prefix_expression(prefix, std::move(operand));
    if (operation == UnaryOperator::BitwiseNot) {
        return restore_integer_type(context, std::move(result), type);
    }
    return result;
}

auto realize_binary(
    ModuleLowering& context,
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

namespace {
auto field_identifier(ModuleLowering& context, StructID owner, std::uint32_t index) noexcept
    -> TargetIdentifier {
    const auto& structure = context.semantic().declarations().structure(owner);
    return context.name_allocator().source(
        context.semantic().provenance().spelling(structure.fields[index].name),
        context.semantic().provenance().spelling(structure.name)
    );
}

auto native_call(
    ModuleLowering& context,
    const SemCppCall& call,
    std::vector<TargetExpr> values
) noexcept -> TargetExpr {
    auto callee = std::visit(
        Overloaded {
            [&](const CppNameReference& name) noexcept -> TargetExpr {
                return name_expression(context.cpp_name(name));
            },
            [&](const CppMemberCallee<SemCppOperand>& member) noexcept -> TargetExpr {
                auto receiver = std::move(values.front());
                values.erase(values.begin());
                return member_expression(
                    std::move(receiver),
                    TargetIdentifier::from_spelling(member.member)
                );
            },
            [&](const SemCppOperand&) noexcept -> TargetExpr {
                auto receiver = std::move(values.front());
                values.erase(values.begin());
                return receiver;
            }
        },
        call.callee
    );
    return call_expression(std::move(callee), std::move(values));
}

auto native_operation(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemCpp& value,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr {
    const auto construct = [&]() noexcept -> TargetExpr {
        auto type_id = context.lower_type(source.type.resolved());
        if (std::holds_alternative<PointerTypeValue>(
                context.semantic().types().type(source.type.resolved()).value
            )) {
            type_id = context.target().intern_type(
                {.value =
                     TargetIntrinsicType {
                         .symbol = TargetSymbol::StdTypeIdentity,
                         .type_argument_ids = {type_id}
                     },
                 .const_qualified = false}
            );
        }
        return {
            .value = TargetConstructionExpr {.type = type_id, .initializer = std::move(arguments)}
        };
    };
    return std::visit(
        Overloaded {
            [&](const CppCStringOperation& literal) noexcept -> TargetExpr {
                return {
                    .value = TargetStaticCastExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .operand = target_child(
                            string_expression(literal.bytes, TargetStringLiteralKind::String)
                        )
                    }
                };
            },
            [&](const CppNameOperation& name) noexcept -> TargetExpr {
                return name_expression(context.cpp_name(name.name));
            },
            [&](const CppConstructOperation&) noexcept -> TargetExpr { return construct(); },
            [&](const CppConvertOperation& conversion) noexcept -> TargetExpr {
                if (source.category == SemanticValueCategory::Place) {
                    return std::move(arguments.front());
                }
                if (conversion.explicit_cast) {
                    return {
                        .value = TargetStaticCastExpr {
                            .type = context.lower_type(source.type.resolved()),
                            .operand = target_child(std::move(arguments.front()))
                        }
                    };
                }
                return construct();
            },
            [&](const CppBinaryOperation& operation) noexcept -> TargetExpr {
                return realize_binary(
                    context,
                    std::move(arguments[0]),
                    operation.operation,
                    std::move(arguments[1]),
                    source.type.resolved()
                );
            },
            [&](const CppUpdateOperation& update) noexcept -> TargetExpr {
                return prefix_expression(
                    update.increment ? TargetPrefixOperator::Increment
                                     : TargetPrefixOperator::Decrement,
                    std::move(arguments.front())
                );
            },
            [&](const CppUnaryOperation& operation) noexcept -> TargetExpr {
                const auto prefix = operation.operation == UnaryOperator::LogicalNot
                    ? TargetPrefixOperator::LogicalNot
                    : operation.operation == UnaryOperator::Negate
                    ? TargetPrefixOperator::Negate
                    : TargetPrefixOperator::BitwiseNot;
                return prefix_expression(prefix, std::move(arguments.front()));
            },
            [&](const CppMemberOperation& member) noexcept -> TargetExpr {
                return member_expression(
                    std::move(arguments.front()),
                    TargetIdentifier::from_spelling(member.name)
                );
            },
            [&](const CppIndexOperation&) noexcept -> TargetExpr {
                return {
                    .value = TargetIndexExpr {
                        .operand = target_child(std::move(arguments[0])),
                        .index = target_child(std::move(arguments[1]))
                    }
                };
            }
        },
        value.operation
    );
}

} // namespace

auto realize_operation(
    ModuleLowering& context,
    const SemanticExpression& source,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr {
    auto result = std::visit(
        Overloaded {
            [&](const SemCppCall& call) noexcept -> TargetExpr {
                return native_call(context, call, std::move(operands));
            },
            [&](const SemCpp& value) noexcept -> TargetExpr {
                return native_operation(context, source, value, std::move(operands));
            },
            [&](const SemConstant& value) noexcept -> TargetExpr {
                return constant_expression(context, value.constant);
            },
            [](const SemBinding&) static noexcept -> TargetExpr {
                invariant_violation("binding storage is resolved by body realization");
            },
            [&](const SemCallable& value) noexcept -> TargetExpr {
                return name_expression(context.callable_name(value.callable));
            },
            [&](const SemEnumConstructor& value) noexcept -> TargetExpr {
                const auto owner =
                    context.semantic().declarations().enum_case(value.enum_case).owner;
                return static_member_expression(
                    context.named_type(context.enumeration_name(owner)),
                    context.names().enum_case_identifier(value.enum_case)
                );
            },
            [](const SemArrayAdopt&) static noexcept -> TargetExpr {
                invariant_violation(
                    "array adoption requires element realization from a stabilized source"
                );
            },
            [&](const SemArray&) noexcept -> TargetExpr {
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
            [&](const SemStruct& value) noexcept -> TargetExpr {
                auto fields = std::vector<std::pair<std::uint32_t, TargetFieldInitializer>>();
                for (auto index = 0uz; index < value.fields.size(); ++index) {
                    const auto& field = value.fields[index];
                    fields.push_back(
                        {field.declaration_index,
                         {.name =
                              field_identifier(context, value.structure, field.declaration_index),
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
            [&](const SemEnumCase& value) noexcept -> TargetExpr {
                return enum_case_expression(context, value.enum_case, std::move(operands));
            },
            [&](const SemUnary& value) noexcept -> TargetExpr {
                return realize_unary(
                    context,
                    value.operation,
                    std::move(operands[0]),
                    source.type.resolved()
                );
            },
            [&](const SemBinary& value) noexcept -> TargetExpr {
                return realize_binary(
                    context,
                    std::move(operands[0]),
                    value.operation,
                    std::move(operands[1]),
                    source.type.resolved()
                );
            },
            [](const SemShortCircuit&) static noexcept -> TargetExpr {
                invariant_violation("short circuit is composed by evaluation");
            },
            [&](const SemCast& value) noexcept -> TargetExpr {
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
            [&](const SemDereference&) noexcept -> TargetExpr {
                return dereference_expression(std::move(operands[0]));
            },
            [&](const SemField& value) noexcept -> TargetExpr {
                return member_expression(
                    std::move(operands[0]),
                    field_identifier(context, value.field.owner, value.field.field_index)
                );
            },
            [&](const SemIndex& value) noexcept -> TargetExpr {
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
            [&](const SemFormat& value) noexcept -> TargetExpr {
                operands.insert(
                    operands.begin(),
                    constant_expression(context, value.format_string_id)
                );
                return call_expression(
                    intrinsic_expression(TargetSymbol::RuntimeFormat),
                    std::move(operands)
                );
            },
            [&](const SemTextIntrinsic& value) noexcept -> TargetExpr {
                if ((value.intrinsic == TextIntrinsic::Bytes
                     || value.intrinsic == TextIntrinsic::Chars)
                    && context.semantic()
                            .types()
                            .type(value.operands[0].expression.type.resolved())
                            .value
                        == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}) {
                    operands[0] = call_member(std::move(operands[0]), "as_str", {});
                }
                switch (value.intrinsic) {
                    case TextIntrinsic::New:
                        return TargetExpr {
                            .value = TargetConstructionExpr {
                                .type = context.lower_type(source.type.resolved()),
                                .initializer = {}
                            }
                        };
                    case TextIntrinsic::FromStr:
                        return call_expression(
                            static_member_expression(
                                context.lower_type(source.type.resolved()),
                                TargetIdentifier::from_spelling("from_str")
                            ),
                            std::move(operands)
                        );
                    case TextIntrinsic::AsStr:
                        return call_member(std::move(operands[0]), "as_str", {});
                    case TextIntrinsic::Append:
                    case TextIntrinsic::Push:
                        return call_member(
                            std::move(operands[0]),
                            value.intrinsic == TextIntrinsic::Append ? "append" : "push",
                            target_expressions(std::move(operands[1]))
                        );
                    case TextIntrinsic::Clear:
                        return call_member(std::move(operands[0]), "clear", {});
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
            [&](const SemClosure&) noexcept -> TargetExpr {
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(operands)
                    }
                };
            },
            [&](const SemBorrowCallable& value) noexcept -> TargetExpr {
                return realize_callable_adaptation(
                    context,
                    std::move(operands.front()),
                    value.source->type.resolved(),
                    source.type.resolved()
                );
            },
            [&](const SemTake&) noexcept -> TargetExpr { return std::move(operands[0]); },
            [&](const SemCall&) noexcept -> TargetExpr {
                auto callee = std::move(operands.front());
                operands.erase(operands.begin());
                return call_expression(std::move(callee), std::move(operands));
            },
            [](const SemIf&) static noexcept -> TargetExpr {
                invariant_violation("structured operation requires body realization");
            },
            [](const SemMatch&) static noexcept -> TargetExpr {
                invariant_violation("structured operation requires body realization");
            },
            [](const SemTry&) static noexcept -> TargetExpr {
                invariant_violation("structured operation requires body realization");
            },
            [](const SemPropagate&) static noexcept -> TargetExpr {
                invariant_violation("propagation forwards its operation");
            }
        },
        source.value
    );
    return result;
}
