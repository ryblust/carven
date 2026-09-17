module carven:backend.lowering.constant.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.symbol;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.provenance;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto integer_suffix(const SemIRProgram& semantic, TypeID type) noexcept -> TargetIntegerSuffix {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic.types().type(type).value);
    if (builtin == nullptr) {
        return TargetIntegerSuffix::None;
    }
    switch (builtin->kind) {
        case BuiltinType::I64:
        case BuiltinType::Isize:        return TargetIntegerSuffix::LongLong;
        case BuiltinType::U64:
        case BuiltinType::Usize:        return TargetIntegerSuffix::UnsignedLongLong;
        case BuiltinType::U8:
        case BuiltinType::U16:
        case BuiltinType::U32:          return TargetIntegerSuffix::Unsigned;
        case BuiltinType::Bool:
        case BuiltinType::Char:
        case BuiltinType::I8:
        case BuiltinType::I16:
        case BuiltinType::I32:
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::String:
        case BuiltinType::Str:
        case BuiltinType::StrCharsView:
        case BuiltinType::Void:
        case BuiltinType::EntryArgs:    return TargetIntegerSuffix::None;
    }
    std::unreachable();
}

template<typename Floating>
auto floating_expression(ModuleLowering& context, Floating value, TypeID type) noexcept
    -> TargetExpr {
    if (std::isfinite(value)) {
        return {.value = TargetLiteralExpr {.value = TargetFloatLiteral {.value = value}}};
    }
    using Bits = std::conditional_t<std::same_as<Floating, float>, std::uint32_t, std::uint64_t>;
    const auto storage =
        std::same_as<Floating, float> ? TargetSymbol::StdUInt32 : TargetSymbol::StdUInt64;
    auto bits = TargetExpr {
        .value = TargetConstructionExpr {
            .type = context.intrinsic_type(storage),
            .initializer = target_expressions(
                TargetExpr {
                    .value = TargetLiteralExpr {
                        .value = TargetIntegerLiteral {
                            .negative = false,
                            .magnitude = std::bit_cast<Bits>(value),
                            .suffix = TargetIntegerSuffix::UnsignedLongLong,
                        },
                    },
                }
            ),
        },
    };
    return template_call_expression(
        intrinsic_expression(TargetSymbol::StdBitCast),
        {context.lower_type(type)},
        target_expressions(std::move(bits))
    );
}

} // namespace

auto typed_integer_expression(
    ModuleLowering& context,
    const IntegerConstant& value,
    TypeID type,
    ConstantLiteralContext use
) noexcept -> TargetExpr {
    auto result = TargetExpr {
        .value = TargetLiteralExpr {
            .value = TargetIntegerLiteral {
                .negative = value.negative(),
                .magnitude = value.magnitude(),
                .suffix = integer_suffix(context.semantic(), type),
            },
        },
    };
    if (use == ConstantLiteralContext::TargetTyped) {
        return result;
    }
    return {
        .value = TargetConstructionExpr {
            .type = context.lower_type(type),
            .initializer = target_expressions(std::move(result)),
        },
    };
}

auto enum_case_index(const SemIRProgram& semantic, EnumCaseID case_id) noexcept -> std::size_t {
    const auto owner = semantic.declarations().enum_case(case_id).owner;
    const auto& cases = semantic.declarations().enumeration(owner).cases;
    const auto found = std::ranges::find(cases, case_id);
    if (found == cases.end()) {
        invariant_violation("enum case is absent from its owner declaration");
    }
    return static_cast<std::size_t>(found - cases.begin());
}

auto enum_case_expression(
    ModuleLowering& context,
    EnumCaseID case_id,
    std::vector<TargetExpr> payload
) noexcept -> TargetExpr {
    const auto owner = context.semantic().declarations().enum_case(case_id).owner;
    const auto owner_type = context.named_type(context.enumeration_name(owner));
    auto member =
        static_member_expression(owner_type, context.names().enum_case_identifier(case_id));
    if (std::holds_alternative<PayloadEnumRepresentation>(
            context.semantic().declarations().enumeration(owner).representation
        )) {
        return call_expression(std::move(member), std::move(payload));
    }
    if (!payload.empty()) {
        invariant_violation("numeric enum case construction has a payload");
    }
    return member;
}

auto constant_expression(
    ModuleLowering& context,
    ConstantID id,
    ConstantLiteralContext use
) noexcept -> TargetExpr {
    const auto& fact = context.semantic().constants().constant(id);
    return fact.value.visit(
        Overloaded {
            [&](const RangeConstant& value) noexcept -> TargetExpr {
                const auto& range =
                    std::get<RangeTypeValue>(context.semantic().types().type(fact.type).value);
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(fact.type),
                        .initializer = target_expressions(
                            typed_integer_expression(
                                context,
                                value.begin,
                                range.element,
                                ConstantLiteralContext::TargetTyped
                            ),
                            typed_integer_expression(
                                context,
                                value.end,
                                range.element,
                                ConstantLiteralContext::TargetTyped
                            ),
                            bool_expression(value.inclusive)
                        )
                    }
                };
            },
            [&](const IntegerConstant& value) noexcept {
                return typed_integer_expression(context, value, fact.type, use);
            },
            [](const BooleanConstant& value) noexcept { return bool_expression(value.value); },
            [&](const StringConstant& value) noexcept {
                return string_expression(
                    std::string(context.semantic().provenance().spelling(value.value)),
                    TargetStringLiteralKind::StringView
                );
            },
            [&](const F32Constant& value) noexcept -> TargetExpr {
                return floating_expression(context, value.value, fact.type);
            },
            [&](const F64Constant& value) noexcept -> TargetExpr {
                return floating_expression(context, value.value, fact.type);
            },
            [](const CharacterConstant& value) noexcept -> TargetExpr {
                return {
                    .value = TargetLiteralExpr {
                        .value = TargetCharacterLiteral {
                            .scalar = value.scalar,
                        },
                    },
                };
            },
            [&](const NullPointerConstant&) noexcept -> TargetExpr {
                return {
                    .value = TargetStaticCastExpr {
                        .type = context.lower_type(fact.type),
                        .operand = target_child(intrinsic_expression(TargetSymbol::StdNullptr)),
                    }
                };
            },
            [&](const NumericEnumConstant& value) noexcept {
                return enum_case_expression(context, value.enum_case, {});
            },
            [&](const PayloadEnumConstant& value) noexcept {
                auto payload = std::vector<TargetExpr>();
                payload.reserve(value.payload.size());
                for (const auto child : value.payload) {
                    payload.push_back(constant_expression(context, child));
                }
                return enum_case_expression(context, value.enum_case, std::move(payload));
            },
            [&](const StructConstant& value) noexcept -> TargetExpr {
                auto fields = std::vector<TargetExpr>();
                fields.reserve(value.fields.size());
                for (const auto child : value.fields) {
                    fields.push_back(
                        constant_expression(context, child, ConstantLiteralContext::TargetTyped)
                    );
                }
                return {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(fact.type),
                        .initializer = std::move(fields),
                    },
                };
            },
            [&](const ArrayConstant& value) noexcept -> TargetExpr {
                const auto* array =
                    std::get_if<ArrayTypeValue>(&context.semantic().types().type(fact.type).value);
                if (array == nullptr) {
                    invariant_violation("array constant has a non-array canonical type");
                }
                auto elements = std::vector<TargetExpr>();
                elements.reserve(value.elements.size());
                for (const auto child : value.elements) {
                    elements.push_back(
                        constant_expression(context, child, ConstantLiteralContext::TargetTyped)
                    );
                }
                return {
                    .value = TargetArrayExpr {
                        .element_type_id = context.lower_type(array->element),
                        .extent = target_child(integer_expression(array->extent)),
                        .elements = std::move(elements),
                    },
                };
            },
            [&](const SliceConstant& value) noexcept -> TargetExpr {
                auto storage = context.constant_storage().find(id);
                if (!storage) {
                    const auto* slice = std::get_if<SliceTypeValue>(
                        &context.semantic().types().type(fact.type).value
                    );
                    if (slice == nullptr) {
                        invariant_violation("slice constant has a non-slice canonical type");
                    }
                    const auto element = context.lower_type(slice->element);
                    auto elements = std::vector<TargetExpr>();
                    elements.reserve(value.elements.size());
                    for (const auto child : value.elements) {
                        elements.push_back(
                            constant_expression(context, child, ConstantLiteralContext::TargetTyped)
                        );
                    }
                    storage = context.constant_storage().append(
                        id,
                        context.intrinsic_type(TargetSymbol::Auto),
                        TargetExpr {
                            .value = TargetArrayExpr {
                                .element_type_id = element,
                                .extent = target_child(integer_expression(value.elements.size())),
                                .elements = std::move(elements),
                            },
                        }
                    );
                }
                return call_expression(
                    intrinsic_expression(TargetSymbol::RuntimeAsSlice),
                    target_expressions(name_expression(std::move(*storage)))
                );
            },
        }
    );
}

auto numeric_enum_case_value_expression(ModuleLowering& context, EnumCaseID enum_case) noexcept
    -> TargetExpr {
    const auto& declaration = context.semantic().declarations().enum_case(enum_case);
    if (!declaration.constant.has_value()) {
        invariant_violation("numeric enum case has no canonical constant");
    }
    const auto& fact = context.semantic().constants().constant(*declaration.constant);
    const auto* numeric = std::get_if<NumericEnumConstant>(&fact.value);
    if (numeric == nullptr || numeric->enum_case != enum_case) {
        invariant_violation("numeric enum case has a non-numeric or foreign canonical constant");
    }
    const auto& enumeration = context.semantic().declarations().enumeration(declaration.owner);
    const auto* representation =
        std::get_if<NumericEnumRepresentation>(&enumeration.representation);
    if (representation == nullptr) {
        invariant_violation("numeric enum case belongs to a payload enum");
    }
    return typed_integer_expression(
        context,
        numeric->value,
        representation->underlying_type,
        ConstantLiteralContext::TargetTyped
    );
}
