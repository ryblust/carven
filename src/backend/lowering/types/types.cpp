module carven:backend.lowering.types.impl;

import :backend.lowering.program;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto named_type(TargetModuleLowerer& context, TargetName name, bool constant) noexcept
    -> TargetTypeID {
    return context.target().intern_type({
        .value =
            TargetNamedType {
                .name = std::move(name),
                .type_argument_ids = {},
                .nested = {},
            },
        .const_qualified = constant,
    });
}

auto intrinsic_type(TargetModuleLowerer& context, TargetSymbol symbol, bool constant) noexcept
    -> TargetTypeID {
    return context.target().intern_type({
        .value = TargetIntrinsicType {.symbol = symbol, .type_argument_ids = {}},
        .const_qualified = constant,
    });
}

auto outcome_type(
    TargetModuleLowerer& context,
    TargetTypeID result,
    std::span<const TargetTypeID> failures
) noexcept -> TargetTypeID {
    auto type_argument_ids = std::vector<TargetTypeID> {result};
    type_argument_ids.reserve(1 + failures.size());
    for (const auto failure : failures) {
        type_argument_ids.emplace_back(failure);
    }
    return context.target().intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::RuntimeOutcome,
                .type_argument_ids = std::move(type_argument_ids),
            },
        .const_qualified = false,
    });
}

auto test_control_type(TargetModuleLowerer& context, TargetTypeID result) noexcept -> TargetTypeID {
    return context.target().intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::TestingControl,
                .type_argument_ids = {result},
            },
        .const_qualified = false,
    });
}

auto reference_type(
    TargetModuleLowerer& context,
    TargetTypeID type,
    bool const_qualified,
    bool rvalue
) noexcept -> TargetTypeID {
    return context.target().intern_type({
        .value =
            TargetReferenceType {
                .referent = type,
                .const_qualified = const_qualified,
                .rvalue = rvalue,
            },
        .const_qualified = false,
    });
}

auto parameter_type(
    TargetModuleLowerer& context,
    HIRAccessMode access,
    HIRTypeID source_type,
    TargetTypeID type
) noexcept -> TargetTypeID {
    const auto* intrinsic = std::get_if<TargetIntrinsicType>(&context.target().type(type).value);
    if (intrinsic != nullptr && intrinsic->symbol == TargetSymbol::Auto) {
        return type;
    }
    switch (access) {
        case HIRAccessMode::Read:
            return context.read_parameter_by_value(source_type)
                ? type
                : reference_type(context, type, true);
        case HIRAccessMode::Write: return reference_type(context, type);
        case HIRAccessMode::Take:  return type;
    }
    std::unreachable();
}

auto materialize_parameter_type(
    TargetModuleLowerer& context,
    const TargetCallParameterRecipe& parameter
) noexcept -> TargetTypeID {
    const auto type = lower_type(context, parameter.type);
    const auto* intrinsic = std::get_if<TargetIntrinsicType>(&context.target().type(type).value);
    if (intrinsic != nullptr && intrinsic->symbol == TargetSymbol::Auto) {
        return type;
    }
    switch (parameter.passing) {
        case TargetParameterPassing::Value:            return type;
        case TargetParameterPassing::ConstReference:   return reference_type(context, type, true);
        case TargetParameterPassing::MutableReference: return reference_type(context, type);
    }
    std::unreachable();
}

auto failure_carrier_type(
    TargetModuleLowerer& context,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> TargetTypeID {
    const auto& members = context.failure_profile(failure_set).ordered_members;
    if (members.empty()) {
        invariant_violation("failure carrier requires at least one failure type");
    }
    const auto failures = members | std::views::transform([&](HIRTypeID failure) noexcept {
                              return lower_type(context, failure);
                          })
        | std::ranges::to<std::vector>();
    return outcome_type(context, lower_type(context, result), failures);
}

auto make_failure_carrier(
    TargetModuleLowerer& context,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> FailureCarrierDescriptor {
    return materialize_failure_carrier(
        context,
        context.source().carrier_shape(result, failure_set)
    );
}

auto materialize_failure_carrier(TargetModuleLowerer& context, TargetCarrierShapeID shape) noexcept
    -> FailureCarrierDescriptor {
    const auto& recipe = context.source().carrier_shape(shape);
    return {
        .shape = shape,
        .type = failure_carrier_type(context, recipe.result, recipe.failure_profile),
    };
}

auto classify_carrier_conversion(
    const TargetModuleLowerer& context,
    const FailureCarrierDescriptor& source,
    const FailureCarrierDescriptor& destination
) noexcept -> TargetCarrierConversion {
    return context.source().classify_carrier_conversion(source.shape, destination.shape);
}

auto is_integer_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool {
    return context.source().type_recipe(id).integer;
}

auto is_void_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool {
    return context.source().type_recipe(id).void_type;
}

auto is_foreign_type(const TargetModuleLowerer& context, HIRTypeID id) noexcept -> bool {
    return context.source().type_recipe(id).foreign;
}

auto lower_type(TargetModuleLowerer& context, HIRTypeID id) noexcept -> TargetTypeID {
    if (const auto cached = context.cached_type(id)) {
        return *cached;
    }
    auto target = std::visit(
        Overloaded {
            [](const TargetIntrinsicTypeRecipe& intrinsic) static noexcept -> TargetType {
                return {
                    .value =
                        TargetIntrinsicType {
                            .symbol = intrinsic.symbol,
                            .type_argument_ids = {},
                        },
                    .const_qualified = false,
                };
            },
            [&](const TargetNamedTypeRecipe& nominal) noexcept -> TargetType {
                return {
                    .value =
                        TargetNamedType {
                            .name = context.entity_name(nominal.symbol),
                            .type_argument_ids = {},
                            .nested = {},
                        },
                    .const_qualified = false,
                };
            },
            [&](const TargetArrayTypeRecipe& array) noexcept -> TargetType {
                return {
                    .value =
                        TargetArrayType {
                            .element_type_id = lower_type(context, array.element),
                            .extent = array.extent,
                        },
                    .const_qualified = false,
                };
            },
            [&](const TargetFunctionReferenceTypeRecipe& function) noexcept -> TargetType {
                const auto& signature = context.source().call_signature(function.signature);
                auto parameters = std::vector<TargetTypeID>();
                parameters.reserve(signature.parameters.size());
                for (const auto& parameter : signature.parameters) {
                    parameters.push_back(materialize_parameter_type(context, parameter));
                }
                const auto result = lower_type(context, signature.result);
                return {
                    .value =
                        TargetFunctionType {
                            .parameters = std::move(parameters),
                            .result = signature.carrier_shape.has_value()
                                ? failure_carrier_type(
                                      context,
                                      signature.result,
                                      signature.failure_profile
                                  )
                                : result,
                        },
                    .const_qualified = false,
                };
            },
            [](const TargetCallableTypeRecipe&) static noexcept -> TargetType {
                return {
                    .value =
                        TargetIntrinsicType {
                            .symbol = TargetSymbol::Auto,
                            .type_argument_ids = {},
                        },
                    .const_qualified = false,
                };
            },
            [](const TargetDeducedTypeRecipe&) static noexcept -> TargetType {
                return {
                    .value =
                        TargetIntrinsicType {
                            .symbol = TargetSymbol::Auto,
                            .type_argument_ids = {},
                        },
                    .const_qualified = false,
                };
            },
        },
        context.source().type_recipe(id).value
    );
    const auto lowered = context.target().intern_type(std::move(target));
    context.cache_type(id, lowered);
    return lowered;
}
auto lower_literal(
    const TargetModuleLowerer& context,
    const HIRLiteralValue& literal,
    HIRTypeID type
) noexcept -> TargetLiteralValue {
    if (const auto* boolean = std::get_if<HIRBooleanLiteralValue>(&literal)) {
        return boolean->value;
    }
    const auto* character = std::get_if<HIRCharacterLiteralValue>(&literal);
    if (character != nullptr) {
        return TargetCharacterLiteral {.scalar = character->scalar};
    }
    if (const auto* string = std::get_if<HIRStrLiteralValue>(&literal)) {
        return TargetStringLiteral {
            .bytes = std::string(context.source().provenance().spelling(string->bytes)),
            .kind = TargetStringLiteralKind::StringView,
        };
    }
    if (const auto* floating = std::get_if<HIRF32LiteralValue>(&literal)) {
        return TargetFloatLiteral {.value = floating->value};
    }
    if (const auto* floating = std::get_if<HIRF64LiteralValue>(&literal)) {
        return TargetFloatLiteral {.value = floating->value};
    }
    const auto& numeric = std::get<HIRIntegerLiteralValue>(literal);
    auto suffix = TargetIntegerSuffix::None;
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&context.source().type(type).value);
    if (builtin != nullptr) {
        if (builtin->kind == HIRBuiltinType::I64 || builtin->kind == HIRBuiltinType::Isize) {
            suffix = TargetIntegerSuffix::LongLong;
        } else if (builtin->kind == HIRBuiltinType::U64 || builtin->kind == HIRBuiltinType::Usize) {
            suffix = TargetIntegerSuffix::UnsignedLongLong;
        } else if (builtin->kind == HIRBuiltinType::U8
                   || builtin->kind == HIRBuiltinType::U16
                   || builtin->kind == HIRBuiltinType::U32) {
            suffix = TargetIntegerSuffix::Unsigned;
        }
    }
    return TargetIntegerLiteral {
        .negative = numeric.negative,
        .magnitude = numeric.magnitude,
        .suffix = suffix,
    };
}

auto builtin_symbol(HIRBuiltinType type) noexcept -> TargetSymbol {
    using enum HIRBuiltinType;
    switch (type) {
        case Bool:         return TargetSymbol::Bool;
        case Char:         return TargetSymbol::Char;
        case Void:         return TargetSymbol::Void;
        case I8:           return TargetSymbol::StdInt8;
        case I16:          return TargetSymbol::StdInt16;
        case I32:          return TargetSymbol::StdInt32;
        case I64:          return TargetSymbol::StdInt64;
        case U8:           return TargetSymbol::StdUInt8;
        case U16:          return TargetSymbol::StdUInt16;
        case U32:          return TargetSymbol::StdUInt32;
        case U64:          return TargetSymbol::StdUInt64;
        case Isize:        return TargetSymbol::StdPtrdiff;
        case Usize:        return TargetSymbol::StdSize;
        case F32:          return TargetSymbol::Float;
        case F64:          return TargetSymbol::Double;
        case Str:          return TargetSymbol::StdStringView;
        case StrBytesView: return TargetSymbol::RuntimeStrBytesView;
        case StrCharsView: return TargetSymbol::RuntimeStrCharsView;
        case EntryArgs:    return TargetSymbol::Auto;
    }
    std::unreachable();
}
