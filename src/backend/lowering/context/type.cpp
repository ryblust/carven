module carven:backend.lowering.context.type.impl;

import :backend.lowering.context;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto builtin_symbol(BuiltinType type) noexcept -> TargetSymbol {
    using enum BuiltinType;
    switch (type) {
        case Bool:         return TargetSymbol::Bool;
        case Char:         return TargetSymbol::Char;
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
        case Void:         return TargetSymbol::Void;
        case EntryArgs:    return TargetSymbol::Auto;
    }
    std::unreachable();
}

} // namespace

auto ModuleLowering::intrinsic_type(TargetSymbol symbol, bool constant) noexcept -> TargetTypeID {
    return target().intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = symbol,
                .type_argument_ids = {},
            },
        .const_qualified = constant,
    });
}

auto ModuleLowering::named_type(TargetName name, bool constant) noexcept -> TargetTypeID {
    return target().intern_type({
        .value =
            TargetNamedType {
                .name = std::move(name),
                .type_argument_ids = {},
                .nested = {},
            },
        .const_qualified = constant,
    });
}

auto ModuleLowering::reference_type(TargetTypeID referent, bool constant, bool rvalue) noexcept
    -> TargetTypeID {
    return target().intern_type({
        .value =
            TargetReferenceType {
                .referent = referent,
                .const_qualified = constant,
                .rvalue = rvalue,
            },
        .const_qualified = false,
    });
}

auto ModuleLowering::pointer_type(TargetTypeID pointee, bool constant) noexcept -> TargetTypeID {
    return target().intern_type({
        .value = TargetPointerType {.pointee = pointee},
        .const_qualified = constant,
    });
}

auto ModuleLowering::optional_type(TargetTypeID value) noexcept -> TargetTypeID {
    return target().intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::StdOptional,
                .type_argument_ids = {value},
            },
        .const_qualified = false,
    });
}

auto ModuleLowering::variant_type(std::span<const TypeID> members) noexcept -> TargetTypeID {
    if (members.empty()) {
        invariant_violation("target variant lowering requires a non-empty failure set");
    }
    auto alternatives = std::vector<TargetTypeID>();
    alternatives.reserve(members.size());
    for (const auto member : members) {
        alternatives.push_back(lower_type(member));
    }
    return target().intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::StdVariant,
                .type_argument_ids = std::move(alternatives),
            },
        .const_qualified = false,
    });
}

auto ModuleLowering::lower_parameter(const CallableParameter& parameter) noexcept -> TargetTypeID {
    const auto base = lower_type(parameter.type);
    const auto* builtin =
        std::get_if<BuiltinTypeValue>(&semantic().types().type(parameter.type).value);
    if (builtin != nullptr && builtin->kind == BuiltinType::EntryArgs) {
        return base;
    }
    switch (parameter.access) {
        case AccessMode::Read:
            return target().intern_type({
                .value =
                    TargetIntrinsicType {
                        .symbol = TargetSymbol::RuntimeReadArg,
                        .type_argument_ids = {base},
                    },
                .const_qualified = false,
            });
        case AccessMode::Write: return reference_type(base);
        case AccessMode::Take:  return base;
    }
    std::unreachable();
}

auto ModuleLowering::function_type(CallableSignatureID id) noexcept -> TargetType {
    const auto& signature = semantic().callable_signatures().signature(id);
    auto parameters = std::vector<TargetTypeID>();
    parameters.reserve(signature.parameters.size());
    for (const auto& parameter : signature.parameters) {
        parameters.push_back(lower_parameter(parameter));
    }
    return {
        .value =
            TargetFunctionType {
                .parameters = std::move(parameters),
                .result = lower_signature_result(id),
            },
        .const_qualified = false,
    };
}

auto ModuleLowering::lower_signature_result(CallableSignatureID id) noexcept -> TargetTypeID {
    if (id.owner() != semantic().identity() || id.index() >= signature_states.size()) {
        invariant_violation("module lowering received an unknown callable signature");
    }
    auto& state = signature_states[id.index()];
    if (state == LoweringState::Complete) {
        return *signature_result_cache[id.index()];
    }
    if (state == LoweringState::Visiting) {
        invariant_violation("recursive callable result reached target lowering");
    }
    state = LoweringState::Visiting;
    const auto& signature = semantic().callable_signatures().signature(id);
    const auto failures = plan().failure_abi().members(signature.failures);
    const auto result = [&]() noexcept -> TargetTypeID {
        if (failures.empty()) {
            return lower_type(signature.result);
        }
        auto arguments = std::vector<TargetTypeID> {lower_type(signature.result)};
        arguments.reserve(failures.size() + 1);
        for (const auto member : failures) {
            arguments.push_back(lower_type(member));
        }
        return target().intern_type({
            .value =
                TargetIntrinsicType {
                    .symbol = TargetSymbol::RuntimeOutcome,
                    .type_argument_ids = std::move(arguments),
                },
            .const_qualified = false,
        });
    }();
    signature_result_cache[id.index()] = result;
    state = LoweringState::Complete;
    return result;
}

auto ModuleLowering::outcome_type(CallableSignatureID signature) noexcept -> TargetTypeID {
    return lower_signature_result(signature);
}

auto ModuleLowering::lower_type(TypeID id) noexcept -> TargetTypeID {
    if (id.owner() != semantic().identity() || id.index() >= type_states.size()) {
        invariant_violation("module lowering received an unknown semantic type");
    }
    auto& state = type_states[id.index()];
    if (state == LoweringState::Complete) {
        return *type_cache[id.index()];
    }
    if (state == LoweringState::Visiting) {
        invariant_violation("recursive structural type reached target lowering");
    }
    state = LoweringState::Visiting;
    auto lowered = std::visit(
        Overloaded {
            [](const BuiltinTypeValue& value) noexcept -> TargetType {
                return {
                    .value =
                        TargetIntrinsicType {
                            .symbol = builtin_symbol(value.kind),
                            .type_argument_ids = {},
                        },
                    .const_qualified = false,
                };
            },
            [&](const StructTypeValue& value) noexcept -> TargetType {
                return {
                    .value =
                        TargetNamedType {
                            .name = structure_name(value.structure),
                            .type_argument_ids = {},
                            .nested = {},
                        },
                    .const_qualified = false,
                };
            },
            [&](const EnumTypeValue& value) noexcept -> TargetType {
                return {
                    .value =
                        TargetNamedType {
                            .name = enumeration_name(value.enumeration),
                            .type_argument_ids = {},
                            .nested = {},
                        },
                    .const_qualified = false,
                };
            },
            [&](const ArrayTypeValue& value) noexcept -> TargetType {
                return {
                    .value =
                        TargetArrayType {
                            .element_type_id = lower_type(value.element),
                            .extent = TargetArrayExtent {.magnitude = value.extent},
                        },
                    .const_qualified = false,
                };
            },
            [&](const FunctionTypeValue& value) noexcept -> TargetType {
                const auto& callable = semantic().declarations().callable(value.callable);
                return function_type(callable.signature);
            },
            [&](const ClosureTypeValue& value) noexcept -> TargetType {
                return {
                    .value =
                        TargetNamedType {
                            .name = closure_type_name(value.callable),
                            .type_argument_ids = {},
                            .nested = {},
                        },
                    .const_qualified = false,
                };
            },
            [&](const CallableViewTypeValue& value) noexcept -> TargetType {
                return function_type(value.signature);
            },
        },
        semantic().types().type(id).value
    );
    const auto result = target().intern_type(std::move(lowered));
    type_cache[id.index()] = result;
    state = LoweringState::Complete;
    return result;
}

auto ModuleLowering::is_void(TypeID id) const noexcept -> bool {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic().types().type(id).value);
    return builtin != nullptr && builtin->kind == BuiltinType::Void;
}

auto ModuleLowering::is_integer(TypeID id) const noexcept -> bool {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic().types().type(id).value);
    return builtin != nullptr && builtin_is_integer(builtin->kind);
}
