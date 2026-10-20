module carven:semantic.analysis.generics.inference.impl;

import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.generic;
import std;

auto ProgramDraft::infer_generic_argument(
    ConstructionTypeRef formal,
    ConstructionTypeRef actual,
    GenericDeclarationID definition,
    std::span<std::optional<GenericArgument>> inferred,
    ProgramOriginID origin
) noexcept -> bool {
    const auto infer_effect = [&](ConstructionFailureRef formal,
                                  ConstructionFailureRef actual) noexcept {
        const auto* generic = std::get_if<GenericFailureID>(&formal);
        if (!generic) {
            return true;
        }
        const auto expression = storage.failure_constraints.generic_failure_copy(*generic);
        if (expression.parameters.size() != 1uz
            || !expression.concrete_members.empty()
            || !expression.guards.empty()) {
            return true;
        }
        const auto& component = expression.parameters.front();
        if (!component.mask.complemented || !component.mask.members.empty()) {
            return true;
        }
        const auto parameter = component.parameter;
        if (parameter.definition != definition || parameter.index >= inferred.size()) {
            return true;
        }
        auto& value = inferred[parameter.index];
        const auto candidate = GenericArgument(actual);
        if (value) {
            const auto* previous = std::get_if<ConstructionFailureRef>(&*value);
            if (!previous) {
                return false;
            }
            if (*previous != actual) {
                require_equal_failures(*previous, actual, origin);
            }
            return true;
        }
        value = candidate;
        return true;
    };
    const auto* generic_formal = std::get_if<GenericTypeID>(&formal);
    if (!generic_formal) {
        return type_shapes_compatible(*this, actual, formal);
    }
    const auto description = this->generic_type_copy(*generic_formal);
    if (const auto* parameter = std::get_if<GenericTypeParameter>(&description)) {
        if (parameter->definition != definition || parameter->index >= inferred.size()) {
            return false;
        }
        const auto candidate = GenericArgument(unwrap_type_alias(*this, actual));
        auto& bound = inferred[parameter->index];
        if (bound && *bound != candidate) {
            return false;
        }
        bound = candidate;
        return true;
    }
    if (const auto* concrete = std::get_if<TypeID>(&description)) {
        return type_shapes_compatible(*this, actual, *concrete);
    }
    if (const auto* array = std::get_if<GenericArrayType>(&description)) {
        const auto shape = sequence_shape(*this, actual);
        return shape
            && shape->kind == SequenceShapeKind::Array
            && shape->extent == array->extent
            && infer_generic_argument(array->element, shape->element, definition, inferred, origin);
    }
    if (const auto* slice = std::get_if<GenericSliceType>(&description)) {
        const auto element = slice_element(*this, actual);
        return element
            && infer_generic_argument(slice->element, *element, definition, inferred, origin);
    }
    if (const auto* sequence = std::get_if<GenericVectorType>(&description)) {
        const auto shape = sequence_shape(*this, actual);
        return shape
            && shape->kind == SequenceShapeKind::Vector
            && infer_generic_argument(
                   sequence->element,
                   shape->element,
                   definition,
                   inferred,
                   origin
            );
    }
    if (const auto* tuple = std::get_if<GenericTupleType>(&description)) {
        auto elements = tuple_elements(*this, actual);
        if (!elements
            && unwrap_type_alias(*this, actual)
                == ConstructionTypeRef(builtin_type(BuiltinType::Void))) {
            elements = std::vector<ConstructionTypeRef>();
        }

        if (!elements || elements->size() != tuple->elements.size()) {
            return false;
        }
        for (auto i = 0uz; i < elements->size(); ++i) {
            if (!infer_generic_argument(
                    tuple->elements[i],
                    (*elements)[i],
                    definition,
                    inferred,
                    origin
                )) {
                return false;
            }
        }
        return true;
    }
    if (const auto* application = std::get_if<GenericNominalApplication>(&description)) {
        auto types = GenericEnvironment();
        if (const auto* generic = std::get_if<GenericTypeID>(&actual)) {
            const auto actual_shape = this->generic_type_copy(*generic);
            const auto* selected = std::get_if<GenericNominalApplication>(&actual_shape);
            if (!selected || selected->definition != application->definition) {
                return false;
            }
            types = selected->arguments;
        } else if (const auto* concrete = std::get_if<TypeID>(&actual)) {
            const auto canonical = this->type_copy(*concrete);
            auto nominal = std::optional<NominalDeclarationRef>();
            if (const auto* record = std::get_if<StructTypeValue>(&canonical.value)) {
                nominal = record->structure;
            }
            if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
                nominal = enumeration->enumeration;
            }
            if (!nominal) {
                return false;
            }
            const auto instance = this->generic_nominal_instance_copy(*nominal);
            if (!instance || instance->definition != application->definition) {
                return false;
            }
            types = generic_environment(instance->arguments);
        } else {
            return false;
        }
        if (types.size() != application->arguments.size()) {
            return false;
        }
        for (auto i = 0uz; i < types.size(); ++i) {
            const auto* formal_type = std::get_if<ConstructionTypeRef>(&application->arguments[i]);
            if (formal_type) {
                const auto* actual_type = std::get_if<ConstructionTypeRef>(&types[i]);
                if (!actual_type
                    || !infer_generic_argument(
                        *formal_type,
                        *actual_type,
                        definition,
                        inferred,
                        origin
                    )) {
                    return false;
                }
            } else if (const auto* effect =
                           std::get_if<ConstructionFailureRef>(&application->arguments[i])) {
                const auto* actual_effect = std::get_if<ConstructionFailureRef>(&types[i]);
                if (!actual_effect || !infer_effect(*effect, *actual_effect)) {
                    return false;
                }
            }
        }
        return true;
    }
    if (const auto* pointer = std::get_if<GenericPointerType>(&description)) {
        const auto shape = pointer_shape(*this, actual);
        return shape
            && shape->access == pointer->access
            && infer_generic_argument(pointer->target, shape->target, definition, inferred, origin);
    }
    if (const auto* native = std::get_if<GenericCppNamedType>(&description)) {
        auto arguments = std::vector<ConstructionTypeRef>();
        actual = unwrap_type_alias(*this, actual);
        if (const auto* generic = std::get_if<GenericTypeID>(&actual)) {
            const auto shape = generic_type_copy(*generic);
            const auto* named = std::get_if<GenericCppNamedType>(&shape);
            if (!named || named->name != native->name) {
                return false;
            }
            arguments = named->arguments;
        } else if (const auto* concrete = std::get_if<TypeID>(&actual)) {
            const auto shape = type_copy(*concrete);
            const auto* value = std::get_if<CppTypeValue>(&shape.value);
            const auto* named = value ? std::get_if<CppNamedType>(&value->form) : nullptr;
            if (!named || named->name != native->name) {
                return false;
            }
            for (const auto argument : named->arguments) {
                arguments.push_back(argument);
            }
        } else {
            return false;
        }
        if (arguments.size() != native->arguments.size()) {
            return false;
        }
        for (auto index = 0uz; index < arguments.size(); ++index) {
            if (!infer_generic_argument(
                    native->arguments[index],
                    arguments[index],
                    definition,
                    inferred,
                    origin
                )) {
                return false;
            }
        }
        return true;
    }
    if (const auto* operation = std::get_if<GenericOperationType>(&description)) {
        const auto shape = operation_shape(*this, actual);
        return shape
            && infer_generic_argument(
                   operation->success,
                   shape->success,
                   definition,
                   inferred,
                   origin
            )
            && infer_effect(operation->failures, shape->failures);
    }
    if (const auto* function = std::get_if<GenericCallableViewType>(&description)) {
        const auto shape = callable_shape(actual);
        if (!shape
            || shape->execution != CallableExecutionKind::Synchronous
            || shape->parameters.size() != function->parameters.size()) {
            return false;
        }
        for (const auto [expected, supplied] :
             std::views::zip(function->parameters, shape->parameters)) {
            if (expected.stage != supplied.stage
                || expected.access != supplied.access
                || !infer_generic_argument(
                    expected.type,
                    supplied.type,
                    definition,
                    inferred,
                    origin
                )) {
                return false;
            }
        }
        return infer_generic_argument(function->result, shape->result, definition, inferred, origin)
            && infer_effect(function->failures, shape->failures);
    }
    return false;
}
