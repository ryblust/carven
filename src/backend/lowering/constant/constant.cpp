module carven:backend.lowering.constant.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.constant.storage;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.simd;
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
        case BuiltinType::U8x32:
        case BuiltinType::F32x8:
        case BuiltinType::Mask8:
        case BuiltinType::Mask32:
        case BuiltinType::U8x16:
        case BuiltinType::F32x4:
        case BuiltinType::Mask4:
        case BuiltinType::Mask16:
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

auto tuple_expression(
    ModuleLowering& context,
    TypeID type,
    std::vector<TargetExpr> elements
) noexcept -> TargetExpr {
    if (context.is_void(type)) {
        if (!elements.empty()) {
            invariant_violation("void tuple has elements");
        }
        return {
            .value = TargetConstructionExpr {
                .type = context.lower_type(type),
                .initializer = std::vector<TargetExpr> {}
            }
        };
    }
    const auto& tuple = std::get<TupleTypeValue>(context.semantic().types().type(type).value);
    if (tuple.elements.size() != elements.size()) {
        invariant_violation("tuple realization differs from its logical arity");
    }
    auto suffix = std::vector<TargetTypeID>();
    auto result = TargetExpr {
        .value = TargetConstructionExpr {
            .type = context.target().intern_type(
                {.value =
                     TargetIntrinsicType {
                         .symbol = TargetSymbol::RuntimeTuple,
                         .type_argument_ids = {}
                     },
                 .const_qualified = false}
            ),
            .initializer = std::vector<TargetExpr> {}
        }
    };
    for (auto index = elements.size(); index != 0uz;) {
        --index;
        suffix.insert(suffix.begin(), context.lower_type(tuple.elements[index]));
        const auto suffix_type = context.target().intern_type(
            {.value =
                 TargetIntrinsicType {
                     .symbol = TargetSymbol::RuntimeTuple,
                     .type_argument_ids = suffix
                 },
             .const_qualified = false}
        );
        result = TargetExpr {
            .value = TargetConstructionExpr {
                .type = suffix_type,
                .initializer = target_expressions(std::move(elements[index]), std::move(result))
            }
        };
    }
    return result;
}

auto constant_has_storage(const SemIRProgram& program, ConstantID id) noexcept -> bool {
    return constant_has_storage(PublishedConstantValues(program), id);
}

namespace {

enum class ConstantOwnership { Owned, Retained };

auto constant_initializer(
    ModuleLowering& context,
    ConstantID id,
    ConstantLiteralContext literal,
    ConstantOwnership ownership
) noexcept -> TargetExpr {
    const auto& fact = context.semantic().constants().constant(id);
    const auto child = [&](ConstantID value) noexcept {
        return constant_initializer(context, value, ConstantLiteralContext::TargetTyped, ownership);
    };
    return fact.value.visit(
        Overloaded {
            [&](const VoidConstant&) noexcept -> TargetExpr {
                return tuple_expression(context, fact.type, {});
            },
            [&](const TupleConstant& value) noexcept -> TargetExpr {
                auto elements = std::vector<TargetExpr>();
                for (const auto element : value.elements) {
                    elements.push_back(child(element));
                }
                return tuple_expression(context, fact.type, std::move(elements));
            },
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
                return typed_integer_expression(context, value, fact.type, literal);
            },
            [&](const SIMDConstant& value) noexcept -> TargetExpr {
                auto inputs = std::vector<TargetExpr>();
                const auto owner =
                    std::get<BuiltinTypeValue>(context.semantic().types().type(fact.type).value)
                        .kind;
                const auto layout = *simd_layout(owner);
                const auto mask = owner == layout.mask;
                // Lane storage is bit-exact, including signed zero and NaN payloads.
                const auto splat =
                    !mask && std::ranges::all_of(value.lanes, [&](auto lane) noexcept {
                        return lane == value.lanes.front();
                    });
                auto bits = 0u;
                const auto input_count = splat ? 1uz : value.lanes.size();
                for (auto i = 0uz; i < input_count; ++i) {
                    if (mask) {
                        if (value.lanes[i] != 0u) {
                            bits |= 1u << i;
                        }
                    } else if (layout.element == BuiltinType::F32) {
                        inputs.push_back(floating_expression(
                            context,
                            std::bit_cast<float>(value.lanes[i]),
                            context.semantic().types().builtin_type(BuiltinType::F32)
                        ));
                    } else {
                        inputs.push_back(
                            TargetExpr {
                                .value = TargetLiteralExpr {
                                    .value = TargetIntegerLiteral {
                                        .negative = false,
                                        .magnitude = value.lanes[i],
                                        .suffix = TargetIntegerSuffix::Unsigned
                                    }
                                }
                            }
                        );
                    }
                }
                if (mask) {
                    inputs.push_back(
                        TargetExpr {
                            .value = TargetLiteralExpr {
                                .value = TargetIntegerLiteral {
                                    .negative = false,
                                    .magnitude = bits,
                                    .suffix = TargetIntegerSuffix::Unsigned
                                }
                            }
                        }
                    );
                }
                return call_expression(
                    static_member_expression(
                        context.lower_type(fact.type),
                        TargetIdentifier::from_spelling(
                            mask        ? "from_bits"
                                : splat ? "splat"
                                        : "from_lanes"
                        )
                    ),
                    std::move(inputs)
                );
            },
            [](const BooleanConstant& value) static noexcept {
                return bool_expression(value.value);
            },
            [&](const StringConstant& value) noexcept {
                auto text = string_expression(
                    std::string(context.semantic().provenance().spelling(value.value)),
                    TargetStringLiteralKind::StringView
                );
                if (context.semantic().types().type(fact.type).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}) {
                    return call_expression(
                        static_member_expression(
                            context.lower_type(fact.type),
                            TargetIdentifier::from_spelling(
                                ownership == ConstantOwnership::Owned ? "from_str" : "from_static"
                            )
                        ),
                        target_expressions(std::move(text))
                    );
                }
                return text;
            },
            [&](const CStringConstant& value) noexcept {
                return TargetExpr {
                    .value = TargetStaticCastExpr {
                        .type = context.lower_type(fact.type),
                        .operand = target_child(string_expression(
                            std::string(context.semantic().provenance().spelling(value.value)),
                            TargetStringLiteralKind::String
                        ))
                    }
                };
            },
            [&](const F32Constant& value) noexcept -> TargetExpr {
                return floating_expression(context, value.value, fact.type);
            },
            [&](const F64Constant& value) noexcept -> TargetExpr {
                return floating_expression(context, value.value, fact.type);
            },
            [](const CharacterConstant& value) static noexcept -> TargetExpr {
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
                for (const auto element : value.payload) {
                    payload.push_back(child(element));
                }
                const auto owner =
                    context.semantic().declarations().enum_case(value.enum_case).owner;
                const auto& representation = context.payload_enum(owner);
                auto record_name = context.enumeration_name(owner);
                record_name.append(
                    representation
                        .case_records[enum_case_index(context.semantic(), value.enum_case)]
                );
                return TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.lower_type(fact.type),
                        .initializer = target_expressions(
                            TargetExpr {
                                .value = TargetConstructionExpr {
                                    .type = context.named_type(record_name),
                                    .initializer = std::move(payload)
                                }
                            }
                        )
                    }
                };
            },
            [&](const StructConstant& value) noexcept -> TargetExpr {
                auto fields = std::vector<TargetExpr>();
                fields.reserve(value.fields.size());
                for (const auto element : value.fields) {
                    fields.push_back(child(element));
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
                for (const auto element : value.elements) {
                    elements.push_back(child(element));
                }
                return {
                    .value = TargetArrayExpr {
                        .element_type_id = context.lower_type(array->element),
                        .extent = target_child(integer_expression(array->extent)),
                        .elements = std::move(elements),
                    },
                };
            },
            [&]<typename Sequence>(const Sequence& value) noexcept -> TargetExpr
                requires (std::same_as<Sequence, SliceConstant>
                          || std::same_as<Sequence, VectorConstant>)
            {
                constexpr auto owning = std::same_as<Sequence, VectorConstant>;
                if constexpr (owning) {
                    if (ownership == ConstantOwnership::Owned) {
                        auto elements = std::vector<TargetExpr>();
                        elements.reserve(value.elements.size());
                        for (const auto element : value.elements) {
                            elements.push_back(constant_initializer(
                                context,
                                element,
                                ConstantLiteralContext::Exact,
                                ownership
                            ));
                        }
                        return TargetExpr {
                            .value = TargetConstructionExpr {
                                .type = context.lower_type(fact.type),
                                .initializer = std::move(elements)
                            }
                        };
                    }
                }
                auto storage = context.constant_storage().find(id);
                if (!storage) {
                    const auto& canonical = context.semantic().types().type(fact.type).value;
                    const auto element = context.lower_type([&]() noexcept {
                        if constexpr (owning) {
                            return std::get<VectorTypeValue>(canonical).element;
                        } else {
                            return std::get<SliceTypeValue>(canonical).element;
                        }
                    }());
                    auto elements = std::vector<TargetExpr>();
                    elements.reserve(value.elements.size());
                    for (const auto child : value.elements) {
                        elements.push_back(constant_initializer(
                            context,
                            child,
                            ConstantLiteralContext::TargetTyped,
                            ConstantOwnership::Retained
                        ));
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
                auto view = call_expression(
                    intrinsic_expression(TargetSymbol::RuntimeAsSlice),
                    target_expressions(name_expression(std::move(*storage)))
                );
                if constexpr (owning) {
                    return call_expression(
                        static_member_expression(
                            context.lower_type(fact.type),
                            TargetIdentifier::from_spelling("from_static")
                        ),
                        target_expressions(std::move(view), integer_expression(value.capacity))
                    );
                } else {
                    return view;
                }
            },
            }
    );
}

} // namespace

auto constant_expression(
    ModuleLowering& context,
    ConstantID id,
    ConstantLiteralContext literal
) noexcept -> TargetExpr {
    return constant_initializer(context, id, literal, ConstantOwnership::Owned);
}

auto constant_reference(ModuleLowering& context, ConstantID id) noexcept -> TargetExpr {
    if (!constant_has_storage(context.semantic(), id)) {
        return constant_expression(context, id);
    }
    auto stored = context.constant_storage().find(id, ConstantStorageRole::Object);
    if (!stored) {
        auto initializer = constant_initializer(
            context,
            id,
            ConstantLiteralContext::TargetTyped,
            ConstantOwnership::Retained
        );
        stored = context.constant_storage().append(
            id,
            context.lower_type(context.semantic().constants().constant(id).type),
            std::move(initializer),
            ConstantStorageRole::Object
        );
    }
    return name_expression(std::move(*stored));
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
