module carven:backend.realization.operation.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation;
import :backend.realization.display;
import :backend.realization.format;
import :backend.realization.operation;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.slice;
import :semantic.semir.structured;
import :semantic.semir.text;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.provenance;
import :support.invariant;
import :support.visit;
import std;

auto realize_callable_adaptation(
    ModuleLowering& context,
    TargetExpr input,
    const PreparedCallableAdaptation& preparation,
    TypeID to
) noexcept -> TargetExpr {
    const auto stateless = preparation.adaptation.kind == CallableAdaptationKind::StatelessClosure;
    if (preparation.array) {
        return template_call_expression(
            intrinsic_expression(TargetSymbol::RuntimeAdoptArray),
            {context.lower_type(to), stateless},
            target_expressions(std::move(input))
        );
    }
    if (stateless) {
        return call_expression(
            static_member_expression(
                context.lower_type(to),
                TargetIdentifier::from_spelling("from_stateless")
            ),
            target_expressions(std::move(input))
        );
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
    const PreparedUnary& preparation,
    TargetExpr operand
) noexcept -> TargetExpr {
    auto result = preparation.operation.visit(
        Overloaded {
            [&](TargetSymbol runtime) noexcept {
                return template_call_expression(
                    intrinsic_expression(runtime),
                    {context.lower_type(preparation.result_type)},
                    target_expressions(std::move(operand))
                );
            },
            [&](TargetPrefixOperator operation) noexcept {
                return prefix_expression(operation, std::move(operand));
            }
        }
    );
    if (preparation.restore_result_type) {
        return {
            .value = TargetStaticCastExpr {
                .type = context.lower_type(preparation.result_type),
                .operand = target_child(std::move(result))
            }
        };
    }
    return result;
}

auto source_site_expression(ModuleLowering& context, ProgramOriginID origin) noexcept
    -> TargetExpr {
    const auto provenance = context.semantic().provenance();
    const auto source = target_source_origin(provenance, origin);
    auto arguments = std::vector<TargetExpr>();
    arguments.push_back(string_expression(source.display_origin, TargetStringLiteralKind::String));
    arguments.push_back(integer_expression(source.line));
    arguments.push_back(integer_expression(provenance.location(origin).column));
    return call_expression(
        intrinsic_expression(TargetSymbol::RuntimeSourceSite),
        std::move(arguments)
    );
}

auto realize_binary(
    ModuleLowering& context,
    const PreparedBinary& preparation,
    TargetExpr left,
    TargetExpr right,
    ProgramOriginID origin
) noexcept -> TargetExpr {
    auto result = preparation.operation.visit(
        Overloaded {
            [&](TargetSymbol runtime) noexcept {
                auto arguments = target_expressions(std::move(left), std::move(right));
                const auto traps = runtime == TargetSymbol::RuntimeIntegerDivide
                    || runtime == TargetSymbol::RuntimeIntegerRemainder
                    || runtime == TargetSymbol::RuntimeIntegerLeftShift
                    || runtime == TargetSymbol::RuntimeIntegerRightShift;
                if (traps) {
                    arguments.push_back(source_site_expression(context, origin));
                }
                return template_call_expression(
                    intrinsic_expression(runtime),
                    {context.lower_type(preparation.result_type)},
                    std::move(arguments)
                );
            },
            [&](TargetBinaryOperator operation) noexcept {
                return binary_expression(std::move(left), operation, std::move(right));
            }
        }
    );
    if (preparation.restore_result_type) {
        return {
            .value = TargetStaticCastExpr {
                .type = context.lower_type(preparation.result_type),
                .operand = target_child(std::move(result))
            }
        };
    }
    return result;
}

namespace {

auto native_call(
    ModuleLowering& context,
    const SemCppCall& call,
    std::vector<TargetExpr> values
) noexcept -> TargetExpr {
    auto callee = call.callee.visit(
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
        }
    );
    return call_expression(std::move(callee), std::move(values));
}

auto native_operation(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemCpp& value,
    const OperationPreparation* preparation,
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
    return value.operation.visit(
        Overloaded {
            [&](const CppNameOperation& name) noexcept -> TargetExpr {
                return name_expression(context.cpp_name(name.name));
            },
            [&](const CppConstructOperation& construction) noexcept -> TargetExpr {
                const auto* plan = std::get_if<PreparedNativeConstruction>(preparation);
                if (plan == nullptr) {
                    invariant_violation("native construction requires preparation");
                }
                auto delivered = std::vector<TargetExpr>();
                for (const auto& argument : plan->arguments) {
                    delivered.push_back(argument.visit(
                        Overloaded {
                            [&](std::size_t index) noexcept {
                                return std::move(arguments.at(index));
                            },
                            [&](const CppConstructArgument* constant) noexcept {
                                return context.cpp_constant_argument(*constant);
                            }
                        }
                    ));
                }
                return {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(construction.target),
                        .initializer = std::move(delivered)
                    }
                };
            },
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
            [&](const CppBinaryOperation&) noexcept -> TargetExpr {
                const auto* plan = std::get_if<PreparedBinary>(preparation);
                if (plan == nullptr) {
                    invariant_violation("binary operation requires preparation");
                }
                return realize_binary(
                    context,
                    *plan,
                    std::move(arguments[0]),
                    std::move(arguments[1]),
                    source.origin
                );
            },
            [&](const CppUpdateOperation& update) noexcept -> TargetExpr {
                return prefix_expression(
                    update.increment ? TargetPrefixOperator::Increment
                                     : TargetPrefixOperator::Decrement,
                    std::move(arguments.front())
                );
            },
            [&](const CppUnaryOperation&) noexcept -> TargetExpr {
                const auto* plan = std::get_if<PreparedUnary>(preparation);
                if (plan == nullptr) {
                    invariant_violation("unary operation requires preparation");
                }
                return realize_unary(context, *plan, std::move(arguments.front()));
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
        }
    );
}

} // namespace

auto realize_operation(
    ModuleLowering& context,
    const SemanticExpression& source,
    const OperationPreparation* preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr {
    auto result = source.value.visit(
        Overloaded {
            [&](const SemCppCall& call) noexcept -> TargetExpr {
                return native_call(context, call, std::move(operands));
            },
            [&](const SemCpp& value) noexcept -> TargetExpr {
                return native_operation(context, source, value, preparation, std::move(operands));
            },
            [&](const SemDefault&) noexcept -> TargetExpr {
                if (std::holds_alternative<PointerTypeValue>(
                        context.semantic().types().type(source.type.resolved()).value
                    )) {
                    return {
                        .value = TargetStaticCastExpr {
                            .type = context.lower_type(source.type.resolved()),
                            .operand = target_child(intrinsic_expression(TargetSymbol::StdNullptr)),
                        }
                    };
                }
                return {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = {}
                    }
                };
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
            [&](const SemRange& value) noexcept -> TargetExpr {
                operands.push_back(bool_expression(value.inclusive));
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(operands)
                    }
                };
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
                              context.field_identifier(value.structure, field.declaration_index),
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
            [&](const SemUnary&) noexcept -> TargetExpr {
                const auto* plan = std::get_if<PreparedUnary>(preparation);
                if (plan == nullptr) {
                    invariant_violation("unary operation requires preparation");
                }
                return realize_unary(context, *plan, std::move(operands[0]));
            },
            [&](const SemBinary&) noexcept -> TargetExpr {
                const auto* plan = std::get_if<PreparedBinary>(preparation);
                if (plan == nullptr) {
                    invariant_violation("binary operation requires preparation");
                }
                return realize_binary(
                    context,
                    *plan,
                    std::move(operands[0]),
                    std::move(operands[1]),
                    source.origin
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
                        target_expressions(
                            std::move(operands[0]),
                            source_site_expression(context, source.origin)
                        )
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
            [&](const SemAddressOf&) noexcept -> TargetExpr {
                return call_expression(
                    intrinsic_expression(TargetSymbol::StdAddressof),
                    target_expressions(std::move(operands[0]))
                );
            },
            [&](const SemField& value) noexcept -> TargetExpr {
                return member_expression(
                    std::move(operands[0]),
                    context.field_identifier(value.field.owner, value.field.field_index)
                );
            },
            [&](const SemIndex& value) noexcept -> TargetExpr {
                if (std::holds_alternative<RuntimeCheckedBounds>(value.bounds)) {
                    // A consumed place takes its consumer's origin; the subscript
                    // keeps the position the executor reports.
                    const auto is_slice = std::holds_alternative<SliceTypeValue>(
                        context.semantic().types().type(value.source->type.resolved()).value
                    );
                    return call_expression(
                        intrinsic_expression(
                            is_slice ? TargetSymbol::RuntimeCheckedSliceIndex
                                     : TargetSymbol::RuntimeCheckedArrayIndex
                        ),
                        target_expressions(
                            std::move(operands[0]),
                            std::move(operands[1]),
                            source_site_expression(context, value.index->origin)
                        )
                    );
                }
                return TargetExpr {
                    .value = TargetIndexExpr {
                        .operand = target_child(std::move(operands[0])),
                        .index = target_child(std::move(operands[1]))
                    }
                };
            },
            [](const SemReport&) static noexcept -> TargetExpr {
                invariant_violation("condition report requires control-flow realization");
            },
            [](const SemUnreachable&) static noexcept -> TargetExpr {
                invariant_violation("unreachable operation has no value realization");
            },
            [&](const SemPrint& value) noexcept -> TargetExpr {
                const auto* prepared = std::get_if<PreparedPrint>(preparation);
                if (prepared != nullptr) {
                    auto selected = std::vector<TargetExpr>();
                    auto next = 0uz;
                    for (const auto& text : prepared->operand_text) {
                        if (text) {
                            selected.push_back(
                                TargetExpr {
                                    .value = TargetLiteralExpr {
                                        .value = TargetStringLiteral {
                                            .bytes = *text,
                                            .kind = TargetStringLiteralKind::StringView
                                        }
                                    }
                                }
                            );
                        } else {
                            selected.push_back(std::move(operands.at(next++)));
                        }
                    }
                    operands = std::move(selected);
                }
                for (auto index = 0uz; index < operands.size(); ++index) {
                    const auto type = value.operands[index].expression.type.resolved();
                    if (!std::holds_alternative<BuiltinTypeValue>(
                            context.semantic().types().type(type).value
                        )) {
                        operands[index] =
                            realize_display(context, type, std::move(operands[index]));
                    }
                }
                const auto symbol = value.kind == PrintKind::Print ? TargetSymbol::RuntimePrint
                    : value.kind == PrintKind::Println             ? TargetSymbol::RuntimePrintln
                    : value.kind == PrintKind::Eprint              ? TargetSymbol::RuntimeEprint
                                                                   : TargetSymbol::RuntimeEprintln;
                return call_expression(intrinsic_expression(symbol), std::move(operands));
            },
            [&](const SemFormat& value) noexcept -> TargetExpr {
                const auto* prepared = std::get_if<PreparedFormat>(preparation);
                if (prepared == nullptr) {
                    invariant_violation("formatting requires an implementation preparation");
                }
                return realize_format(context, source, value, *prepared, std::move(operands));
            },
            [&](const SemIntrinsic& value) noexcept -> TargetExpr {
                return value.operation.visit(
                    Overloaded {
                        [&](const SliceIntrinsicOperation& family) noexcept -> TargetExpr {
                            const auto method = [&](const char* name) noexcept -> TargetExpr {
                                auto receiver = std::move(operands.front());
                                operands.erase(operands.begin());
                                return call_member(std::move(receiver), name, std::move(operands));
                            };
                            switch (family.intrinsic) {
                                case SliceIntrinsic::FromArray:
                                    return call_expression(
                                        intrinsic_expression(TargetSymbol::RuntimeAsSlice),
                                        std::move(operands)
                                    );
                                case SliceIntrinsic::Len:     return method("size");
                                case SliceIntrinsic::IsEmpty: return method("empty");
                                case SliceIntrinsic::Slice:
                                    operands.push_back(
                                        source_site_expression(context, source.origin)
                                    );
                                    return method("slice");
                            }
                            std::unreachable();
                        },
                        [&](const SIMDIntrinsic& family) noexcept -> TargetExpr {
                            const auto owner = simd_owner(
                                family,
                                source.type.resolved(),
                                value.operands.front().expression.type.resolved(),
                                [&](TypeID type) noexcept -> const CanonicalType& {
                                    return context.semantic().types().type(type);
                                }
                            );
                            const auto contract = simd_contract(family, owner);
                            if (const auto* lane = std::get_if<PreparedSIMDLane>(preparation)) {
                                auto receiver = std::move(operands.front());
                                operands.erase(operands.begin(), operands.begin() + 2);
                                return template_call_expression(
                                    TargetExpr {
                                        .value =
                                            TargetMemberExpr {
                                                .operand = target_child(std::move(receiver)),
                                                .name =
                                                    TargetIdentifier::from_spelling(contract.name)
                                            }
                                    },
                                    {TargetIntegerLiteral {
                                        .negative = false,
                                        .magnitude = lane->index,
                                        .suffix = TargetIntegerSuffix::None
                                    }},
                                    std::move(operands)
                                );
                            }
                            if (simd_reports(family)) {
                                operands.push_back(source_site_expression(context, source.origin));
                            }
                            if (simd_is_factory(family)) {
                                return call_expression(
                                    static_member_expression(
                                        context.lower_type(source.type.resolved()),
                                        TargetIdentifier::from_spelling(contract.name)
                                    ),
                                    std::move(operands)
                                );
                            }
                            auto receiver = std::move(operands.front());
                            operands.erase(operands.begin());
                            if (const auto input = simd_static_input(family)) {
                                const auto id = value.operands[*input].expression.constant;
                                if (!id) {
                                    invariant_violation("SIMD immediate was not realized");
                                }
                                const auto& control = std::get<IntegerConstant>(
                                    context.semantic().constants().constant(*id).value
                                );
                                operands.erase(
                                    operands.begin() + static_cast<std::ptrdiff_t>(*input - 1uz)
                                );
                                auto callee = TargetExpr {
                                    .value = TargetMemberExpr {
                                        .operand = target_child(std::move(receiver)),
                                        .name = TargetIdentifier::from_spelling(contract.name)
                                    }
                                };
                                return template_call_expression(
                                    std::move(callee),
                                    {TargetIntegerLiteral {
                                        .negative = false,
                                        .magnitude = control.magnitude(),
                                        .suffix = TargetIntegerSuffix::None
                                    }},
                                    std::move(operands)
                                );
                            }
                            return call_member(
                                std::move(receiver),
                                contract.name,
                                std::move(operands)
                            );
                        },
                        [&](const TextIntrinsic& family) noexcept -> TargetExpr {
                            switch (family) {
                                case TextIntrinsic::FromStr:
                                    return call_expression(
                                        static_member_expression(
                                            context.lower_type(source.type.resolved()),
                                            TargetIdentifier::from_spelling("from_str")
                                        ),
                                        std::move(operands)
                                    );
                                case TextIntrinsic::FromU32Unchecked:
                                    return TargetExpr {
                                        .value = TargetStaticCastExpr {
                                            .type = context.lower_type(source.type.resolved()),
                                            .operand = target_child(std::move(operands[0]))
                                        }
                                    };
                                case TextIntrinsic::FromUTF8Unchecked:
                                    return call_expression(
                                        intrinsic_expression(TargetSymbol::RuntimeUTF8Text),
                                        std::move(operands)
                                    );
                                case TextIntrinsic::AsStr:
                                    return call_member(std::move(operands[0]), "as_str", {});
                                case TextIntrinsic::Append:
                                case TextIntrinsic::Push:
                                    return call_member(
                                        std::move(operands[0]),
                                        family == TextIntrinsic::Append ? "append" : "push",
                                        target_expressions(std::move(operands[1]))
                                    );
                                case TextIntrinsic::Clear:
                                    return call_member(std::move(operands[0]), "clear", {});
                                case TextIntrinsic::Len:
                                    return call_member(std::move(operands[0]), "size", {});
                                case TextIntrinsic::IsEmpty:
                                    return call_member(std::move(operands[0]), "empty", {});
                                case TextIntrinsic::Bytes:
                                    return call_expression(
                                        intrinsic_expression(TargetSymbol::RuntimeTextBytes),
                                        target_expressions(std::move(operands[0]))
                                    );
                                case TextIntrinsic::Chars:
                                    return call_expression(
                                        intrinsic_expression(TargetSymbol::RuntimeTextChars),
                                        target_expressions(std::move(operands[0]))
                                    );
                            }
                            std::unreachable();
                        }
                    }
                );
            },
            [&](const SemClosure&) noexcept -> TargetExpr {
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(source.type.resolved()),
                        .initializer = std::move(operands)
                    }
                };
            },
            [&](const SemBorrowCallable&) noexcept -> TargetExpr {
                const auto* plan = std::get_if<PreparedCallableAdaptation>(preparation);
                if (plan == nullptr) {
                    invariant_violation("callable adaptation requires preparation");
                }
                return realize_callable_adaptation(
                    context,
                    std::move(operands.front()),
                    *plan,
                    source.type.resolved()
                );
            },
            [&](const SemTake&) noexcept -> TargetExpr { return std::move(operands[0]); },
            [&](const SemCall& value) noexcept -> TargetExpr {
                if (value.target) {
                    return call_expression(
                        name_expression(context.callable_name(*value.target)),
                        std::move(operands)
                    );
                }
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
        }
    );
    return result;
}

auto discarded_operation(
    const ModuleLowering& context,
    const SemanticExpression& source,
    TargetExpr expression
) noexcept -> TargetStmt {
    auto implicit = context.is_void(source.type.resolved());
    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
    if (!implicit
        && call != nullptr
        && !std::holds_alternative<CppTypeValue>(
            context.semantic().types().type(source.type.resolved()).value
        )
        && !std::holds_alternative<SemCpp>(source.value)
        && !std::holds_alternative<SemCppCall>(source.value)) {
        const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(&call->callee->value);
        implicit =
            intrinsic == nullptr || target_symbol_info(intrinsic->symbol).allows_implicit_discard;
    }
    if (implicit) {
        return generated_statement(TargetExprStmt {.expression = std::move(expression)});
    }
    return generated_statement(TargetDiscardStmt {.expression = std::move(expression)});
}
