module carven:backend.generation.program.representation.impl;

import :backend.generation.program.construction;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.place;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

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

auto is_integer(HIRBuiltinType type) noexcept -> bool {
    using enum HIRBuiltinType;
    switch (type) {
        case I8:
        case I16:
        case I32:
        case I64:
        case U8:
        case U16:
        case U32:
        case U64:
        case Isize:
        case Usize: return true;
        default:    return false;
    }
}

auto reads_by_value(const SemanticProgram& semantic, const HIRType& type) noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const HIRBuiltinTypeValue&) static noexcept { return true; },
            [](const HIRStructTypeValue&) static noexcept { return false; },
            [&](const HIREnumTypeValue& nominal) noexcept {
                return semantic.enumeration(nominal.enumeration).profile == HIREnumProfile::Numeric;
            },
            [](const HIRArrayTypeValue&) static noexcept { return false; },
            [](const HIRFunctionTypeValue&) static noexcept { return false; },
            [](const HIRFunctionRefTypeValue&) static noexcept { return false; },
            [](const HIRClosureTypeValue&) static noexcept { return false; },
            [](const HIRForeignTypeValue&) static noexcept { return false; },
            [](const HIRErrorTypeValue&) static noexcept { return false; },
        },
        type.value
    );
}

auto parameter_passing(
    const HIRFunctionParameterType& parameter,
    bool read_parameter_by_value
) noexcept -> TargetParameterPassing {
    switch (parameter.access) {
        case HIRAccessMode::Read:
            return read_parameter_by_value ? TargetParameterPassing::Value
                                           : TargetParameterPassing::ConstReference;
        case HIRAccessMode::Write: return TargetParameterPassing::MutableReference;
        case HIRAccessMode::Take:  return TargetParameterPassing::Value;
    }
    std::unreachable();
}

auto failure_order_key(const SemanticProgram& semantic, HIRTypeID id) noexcept
    -> std::tuple<std::string, std::string, std::uint8_t> {
    const auto& type = semantic.type(id).value;
    auto nominal_symbol = std::optional<SymbolID>();
    auto nominal_kind = std::uint8_t();
    if (const auto* structure = std::get_if<HIRStructTypeValue>(&type)) {
        nominal_symbol = semantic.structure(structure->structure).symbol;
        nominal_kind = 0u;
    } else if (const auto* enumeration = std::get_if<HIREnumTypeValue>(&type)) {
        nominal_symbol = semantic.enumeration(enumeration->enumeration).symbol;
        nominal_kind = 1u;
    }
    if (!nominal_symbol.has_value()) {
        invariant_violation("target failure profile contains a non-nominal type");
    }
    const auto& symbol = semantic.symbol(*nominal_symbol);
    if (!symbol.module_id.has_value()) {
        invariant_violation("target failure nominal has no owning module");
    }
    auto names = std::vector<std::string_view>();
    auto current = std::optional {*nominal_symbol};
    while (current.has_value()) {
        const auto& current_symbol = semantic.symbol(*current);
        names.push_back(semantic.provenance().spelling(current_symbol.name));
        current = current_symbol.parent;
    }
    std::ranges::reverse(names);
    auto qualified_name = std::string();
    for (const auto name : names) {
        if (!qualified_name.empty()) {
            qualified_name += "::";
        }
        qualified_name += name;
    }
    return {
        std::string(semantic.provenance().module_record(*symbol.module_id).path.value()),
        std::move(qualified_name),
        nominal_kind,
    };
}

auto failure_subset(
    const TargetFailureProfile& source,
    const TargetFailureProfile& destination
) noexcept -> bool {
    return std::ranges::all_of(source.ordered_members, [&](HIRTypeID failure) noexcept {
        return std::ranges::contains(destination.ordered_members, failure);
    });
}

} // namespace

auto TargetProgramBuilder::intern_carrier_shape(
    HIRTypeID result,
    FailureSetID failure_profile
) noexcept -> TargetCarrierShapeID {
    if (failure_profile.index() >= failure_profiles.size()
        || failure_profiles[failure_profile.index()].ordered_members.empty()) {
        invariant_violation("target carrier shape requires a non-empty failure profile");
    }
    const auto key = std::pair {result.index(), failure_profile.index()};
    if (const auto found = carrier_index.find(key); found != carrier_index.end()) {
        return found->second;
    }
    const auto id =
        TargetCarrierShapeID::from_index(static_cast<std::uint32_t>(carrier_shapes.size()));
    carrier_shapes.push_back({.result = result, .failure_profile = failure_profile});
    carrier_index.emplace(key, id);
    return id;
}

auto TargetProgramBuilder::derive_representations() noexcept -> void {
    auto read_parameter_by_value = std::vector<bool>();
    read_parameter_by_value.reserve(semantic.types().size());
    for (const auto& type : semantic.types()) {
        read_parameter_by_value.push_back(reads_by_value(semantic, type));
    }

    failure_profiles.reserve(semantic.failure_sets().size());
    for (const auto& semantic_set : semantic.failure_sets()) {
        auto members = semantic_set.members;
        std::ranges::sort(members, [&](HIRTypeID left, HIRTypeID right) noexcept {
            return failure_order_key(semantic, left) < failure_order_key(semantic, right);
        });
        for (const auto& [previous, current] : members | std::views::adjacent<2>) {
            if (failure_order_key(semantic, previous) == failure_order_key(semantic, current)) {
                invariant_violation("distinct failure nominals have the same target identity");
            }
        }
        failure_profiles.push_back({.ordered_members = std::move(members)});
    }

    const auto failure_count = failure_profiles.size();
    carrier_conversions.resize(
        failure_count * failure_count,
        std::numeric_limits<std::uint8_t>::max()
    );
    for (auto source = 0uz; source < failure_count; ++source) {
        for (auto destination = 0uz; destination < failure_count; ++destination) {
            if (source == destination) {
                carrier_conversions[source * failure_count + destination] =
                    static_cast<std::uint8_t>(TargetCarrierConversion::Identity);
            } else if (failure_profiles[source].ordered_members.size()
                           < failure_profiles[destination].ordered_members.size()
                       && failure_subset(failure_profiles[source], failure_profiles[destination])) {
                carrier_conversions[source * failure_count + destination] =
                    static_cast<std::uint8_t>(TargetCarrierConversion::Widen);
            }
        }
    }

    const auto append_signature = [&](std::span<const HIRFunctionParameterType> parameters,
                                      HIRTypeID result,
                                      FailureSetID failure_set) noexcept {
        auto target_parameters = std::vector<TargetCallParameterRecipe>();
        target_parameters.reserve(parameters.size());
        for (const auto& parameter : parameters) {
            target_parameters.push_back({
                .type = parameter.type,
                .passing =
                    parameter_passing(parameter, read_parameter_by_value[parameter.type.index()]),
            });
        }
        const auto carrier = failure_profiles[failure_set.index()].ordered_members.empty()
            ? std::optional<TargetCarrierShapeID>()
            : std::optional {intern_carrier_shape(result, failure_set)};
        const auto existing = std::ranges::find_if(
            call_signatures,
            [&](const TargetCallSignatureRecipe& signature) noexcept {
                return signature.result == result
                    && signature.failure_profile == failure_set
                    && signature.carrier_shape == carrier
                    && std::ranges::equal(
                           signature.parameters,
                           target_parameters,
                           [](const TargetCallParameterRecipe& left,
                              const TargetCallParameterRecipe& right) static noexcept {
                               return left.type == right.type && left.passing == right.passing;
                           }
                    );
            }
        );
        if (existing != call_signatures.end()) {
            return TargetCallSignatureID::from_index(
                static_cast<std::uint32_t>(std::ranges::distance(call_signatures.begin(), existing))
            );
        }
        const auto id =
            TargetCallSignatureID::from_index(static_cast<std::uint32_t>(call_signatures.size()));
        call_signatures.push_back({
            .parameters = std::move(target_parameters),
            .result = result,
            .failure_profile = failure_set,
            .carrier_shape = carrier,
        });
        return id;
    };

    callable_signatures.reserve(semantic.callables().size());
    for (auto index = 0uz; index < semantic.callables().size(); ++index) {
        const auto callable_id = CallableID::from_index(static_cast<std::uint32_t>(index));
        const auto& callable = semantic.callable(callable_id);
        callable_signatures.push_back(append_signature(
            callable.parameters,
            callable.result,
            semantic.callable_flow(callable_id).effective_failure_set
        ));
    }
    function_reference_signatures.reserve(semantic.callable_signatures().size());
    for (const auto& signature : semantic.callable_signatures()) {
        function_reference_signatures.push_back(
            append_signature(signature.parameters, signature.result, signature.failure_set)
        );
    }

    types.reserve(semantic.types().size());
    for (const auto& [index, type] : std::views::enumerate(semantic.types())) {
        auto recipe = std::visit(
            Overloaded {
                [](const HIRBuiltinTypeValue& value) static noexcept -> TargetTypeRecipeValue {
                    return TargetIntrinsicTypeRecipe {.symbol = builtin_symbol(value.kind)};
                },
                [&](const HIRStructTypeValue& value) noexcept -> TargetTypeRecipeValue {
                    return TargetNamedTypeRecipe {
                        .symbol = semantic.structure(value.structure).symbol,
                    };
                },
                [&](const HIREnumTypeValue& value) noexcept -> TargetTypeRecipeValue {
                    return TargetNamedTypeRecipe {
                        .symbol = semantic.enumeration(value.enumeration).symbol,
                    };
                },
                [](const HIRArrayTypeValue& value) static noexcept -> TargetTypeRecipeValue {
                    return TargetArrayTypeRecipe {
                        .element = value.element_type_id,
                        .extent = TargetArrayExtent {.magnitude = value.extent},
                    };
                },
                [&](const HIRFunctionTypeValue& value) noexcept -> TargetTypeRecipeValue {
                    return TargetCallableTypeRecipe {
                        .signature = callable_signatures[value.callable.index()],
                    };
                },
                [&](const HIRFunctionRefTypeValue& value) noexcept -> TargetTypeRecipeValue {
                    return TargetFunctionReferenceTypeRecipe {
                        .signature = function_reference_signatures[value.signature.index()],
                    };
                },
                [&](const HIRClosureTypeValue& value) noexcept -> TargetTypeRecipeValue {
                    return TargetCallableTypeRecipe {
                        .signature = callable_signatures[value.callable.index()],
                    };
                },
                [](const HIRForeignTypeValue&) static noexcept -> TargetTypeRecipeValue {
                    return TargetDeducedTypeRecipe {};
                },
                [](const HIRErrorTypeValue&) static noexcept -> TargetTypeRecipeValue {
                    invariant_violation("error type reached target program construction");
                },
            },
            type.value
        );
        const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&type.value);
        types.push_back({
            .value = std::move(recipe),
            .read_parameter_by_value = read_parameter_by_value[index],
            .integer = builtin != nullptr && is_integer(builtin->kind),
            .void_type = builtin != nullptr && builtin->kind == HIRBuiltinType::Void,
            .foreign = std::holds_alternative<HIRForeignTypeValue>(type.value),
        });
    }

    for (auto index = 0uz; index < semantic.expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto failure = semantic.expression_control(id).evaluation_failure_set;
        if (!failure_profiles[failure.index()].ordered_members.empty()) {
            static_cast<void>(intern_carrier_shape(semantic.expression(id).type, failure));
        }
        const auto* attempt = std::get_if<HIRTryExpr>(&semantic.expression(id).value);
        if (attempt == nullptr) {
            continue;
        }
        const auto& protected_block = semantic.block(attempt->body);
        const auto protected_failure = semantic.block_control(attempt->body).outward_failure_set;
        if (failure_profiles[protected_failure.index()].ordered_members.empty()) {
            continue;
        }
        const auto protected_result = protected_block.result.has_value()
            ? semantic.expression(*protected_block.result).type
            : semantic.expression(id).type;
        static_cast<void>(intern_carrier_shape(protected_result, protected_failure));
    }
}

auto TargetProgramBuilder::derive_value_binding_requirements() noexcept -> void {
    mutable_value_binding_flags.resize(semantic.symbols().size(), 0u);
    for (auto index = 0uz; index < semantic.expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& use = semantic.place_use(id);
        if (use.has_value() && use->access != SemanticPlaceAccess::Read) {
            mutable_value_binding_flags[use->root.index()] = 1u;
        }
    }
}
