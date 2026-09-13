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
        case String:       return TargetSymbol::RuntimeString;
        case Str:          return TargetSymbol::StdStringView;
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
            if (plan().read_borrows_storage(parameter.type)) {
                return reference_type(base, true);
            }
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

auto ModuleLowering::function_type(CallableSignatureID id, bool stops_test) noexcept -> TargetType {
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
                .result = lower_signature_result(id, stops_test),
            },
        .const_qualified = false,
    };
}

auto ModuleLowering::lower_signature_result(CallableSignatureID id, bool stops_test) noexcept
    -> TargetTypeID {
    if (id.owner() != semantic().identity()
        || id.index() >= semantic().callable_signatures().size()) {
        invariant_violation("module lowering received an unknown callable signature");
    }
    const auto [entry, inserted] = signature_result_cache.try_emplace(std::pair(id, stops_test));
    auto& state = entry->second;
    if (const auto* complete = std::get_if<TargetTypeID>(&state)) {
        return *complete;
    }
    if (!inserted) {
        invariant_violation("recursive callable result reached target lowering");
    }
    const auto& signature = semantic().callable_signatures().signature(id);
    const auto failures = plan().failure_abi().members(signature.failures);
    const auto result = [&]() noexcept -> TargetTypeID {
        if (failures.empty() && !stops_test) {
            return lower_type(signature.result);
        }
        auto arguments = std::vector<TargetTypeID> {lower_type(signature.result)};
        arguments.reserve(failures.size() + 2);
        if (stops_test) {
            arguments.push_back(intrinsic_type(TargetSymbol::RuntimeTestStopped));
        }
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
    state = result;
    return result;
}

auto ModuleLowering::callable_result(CallableID callable_id) noexcept -> TargetTypeID {
    return lower_signature_result(
        semantic().declarations().callable(callable_id).signature,
        semantic().may_stop_test(callable_id)
    );
}

auto ModuleLowering::call_result(TypeID type) noexcept -> TargetTypeID {
    return lower_signature_result(semantic().call_signature(type), semantic().may_stop_test(type));
}

auto ModuleLowering::lower_type(TypeID id) noexcept -> TargetTypeID {
    if (id.owner() != semantic().identity() || id.index() >= semantic().types().size()) {
        invariant_violation("module lowering received an unknown semantic type");
    }
    const auto [entry, inserted] = type_cache.try_emplace(id);
    auto& state = entry->second;
    if (const auto* complete = std::get_if<TargetTypeID>(&state)) {
        return *complete;
    }
    if (!inserted) {
        invariant_violation("recursive structural type reached target lowering");
    }
    auto lowered = std::visit(
        Overloaded {
            [&](const PointerTypeValue& value) noexcept -> TargetType {
                auto pointee = lower_type(value.target);
                if (value.access == PointerAccess::Read) {
                    pointee = target().intern_type(
                        {.value =
                             TargetIntrinsicType {
                                 .symbol = TargetSymbol::StdAddConst,
                                 .type_argument_ids = {pointee}
                             },
                         .const_qualified = false}
                    );
                }
                return {.value = TargetPointerType {.pointee = pointee}, .const_qualified = false};
            },
            [&](const CppTypeValue& value) noexcept -> TargetType {
                if (std::holds_alternative<CppConstCharPointerType>(value.form)) {
                    return {
                        .value =
                            TargetPointerType {
                                .pointee = intrinsic_type(TargetSymbol::CChar, true)
                            },
                        .const_qualified = false,
                    };
                }
                if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                    auto arguments = std::vector<TargetTypeID>();
                    for (const auto argument : named->arguments) {
                        arguments.push_back(lower_type(argument));
                    }
                    return {
                        .value =
                            TargetNamedType {
                                .name = cpp_name(named->name),
                                .type_argument_ids = std::move(arguments),
                                .nested = {}
                            },
                        .const_qualified = false
                    };
                }
                const auto result = target().intern_type(
                    {.value =
                         TargetDecltypeType {cpp_type_query(std::get<CppQueryType>(value.form))},
                     .const_qualified = false}
                );
                return {
                    .value =
                        TargetIntrinsicType {
                            .symbol = TargetSymbol::StdRemoveCVRef,
                            .type_argument_ids = {result}
                        },
                    .const_qualified = false
                };
            },
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
            [&](const SliceTypeValue& value) noexcept -> TargetType {
                return {
                    .value =
                        TargetIntrinsicType {
                            .symbol = TargetSymbol::RuntimeSlice,
                            .type_argument_ids = {lower_type(value.element)},
                        },
                    .const_qualified = false
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
                return function_type(callable.signature, semantic().may_stop_test(value.callable));
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
                return function_type(value.signature, true);
            },
        },
        semantic().types().type(id).value
    );
    const auto result = target().intern_type(std::move(lowered));
    state = result;
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
