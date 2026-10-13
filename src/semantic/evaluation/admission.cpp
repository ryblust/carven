module carven:semantic.evaluation.admission.impl;

import :semantic.evaluation.admission;
import :semantic.semir.simd;
import std;

auto supported_execution_type(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> bool {
    auto pending = std::vector<ConstructionTypeRef> {type};
    auto visited = std::set<ConstructionTypeRef>();
    while (!pending.empty()) {
        const auto current = execution_type_reference(values, pending.back());
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto supported =
            visit_execution_type(values, current, [&](const auto& shape) noexcept {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, ArrayTypeValue>
                              || std::same_as<Shape, ConstructionArrayTypeValue>
                              || std::same_as<Shape, GenericArrayType>
                              || std::same_as<Shape, VectorTypeValue>
                              || std::same_as<Shape, GenericVectorType>) {
                    pending.push_back(shape.element);
                    return true;
                } else if constexpr (std::same_as<Shape, TupleTypeValue>
                                     || std::same_as<Shape, ConstructionTupleTypeValue>
                                     || std::same_as<Shape, GenericTupleType>) {
                    pending.insert(pending.end(), shape.elements.begin(), shape.elements.end());
                    return true;
                } else if constexpr (std::same_as<Shape, StructTypeValue>) {
                    const auto fields = values.struct_field_types(shape.structure);
                    if (!fields) {
                        return false;
                    }
                    pending.insert(pending.end(), fields->begin(), fields->end());
                    return true;
                } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                    const auto cases = values.enum_case_types(shape.enumeration);
                    if (!cases) {
                        return false;
                    }
                    for (const auto& item : *cases) {
                        pending.insert(
                            pending.end(),
                            item.payload_types.begin(),
                            item.payload_types.end()
                        );
                    }
                    return true;
                } else if constexpr (std::same_as<Shape, CppTypeValue>) {
                    return std::holds_alternative<CppConstCharPointerType>(shape.form);
                } else if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
                    return builtin_is_numeric(shape.kind)
                        || simd_layout(shape.kind)
                        || shape.kind == BuiltinType::Bool
                        || shape.kind == BuiltinType::Char
                        || shape.kind == BuiltinType::Str
                        || shape.kind == BuiltinType::String
                        || shape.kind == BuiltinType::Void;
                } else {
                    // Views and operation descriptors do not contain target storage.
                    return std::same_as<Shape, PointerTypeValue>
                        || std::same_as<Shape, GenericPointerType>
                        || std::same_as<Shape, SliceTypeValue>
                        || std::same_as<Shape, ConstructionSliceTypeValue>
                        || std::same_as<Shape, GenericSliceType>
                        || std::same_as<Shape, FunctionTypeValue>
                        || std::same_as<Shape, CallableViewTypeValue>
                        || std::same_as<Shape, ConstructionCallableViewTypeValue>
                        || std::same_as<Shape, RangeTypeValue>
                        || std::same_as<Shape, OperationTypeValue>
                        || std::same_as<Shape, ConstructionOperationTypeValue>
                        || std::same_as<Shape, GenericOperationType>;
                }
            });
        if (!supported) {
            return false;
        }
    }
    return true;
}

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view> {
    return source.value.visit([](const auto& operation) static noexcept {
        return unsupported_execution_operation(operation);
    });
}

auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view> {
    return source.value.visit(
        [](const auto& value) static noexcept -> std::optional<std::string_view> {
            using Operation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Operation, SemReturn>
                          || std::same_as<Operation, SemThrow>
                          || std::same_as<Operation, SemRethrow>
                          || std::same_as<Operation, SemBreak>
                          || std::same_as<Operation, SemContinue>
                          || std::same_as<Operation, SemExpressionStatement>
                          || std::same_as<Operation, SemAsyncLet>
                          || std::same_as<Operation, SemDecompose>
                          || std::same_as<Operation, SemInitialize>
                          || std::same_as<Operation, SemStaticBinding>
                          || std::same_as<Operation, SemConstBlock>
                          || std::same_as<Operation, SemAssign>
                          || std::same_as<Operation, SemLoop>
                          || std::same_as<Operation, SemRangeLoop>
                          || std::same_as<Operation, SemExpandedLoop>
                          || std::same_as<Operation, OwnedSemanticRegion>) {
                return std::nullopt;
            } else {
                return "control operation is not supported in execution";
            }
        }
    );
}
