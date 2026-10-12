module carven:semantic.semir.initialization;

import :semantic.semir.generic;
import :semantic.semir.program;
import :semantic.semir.type;
import std;

enum class DefaultInitialization { Unavailable, Value, Native };

using InitializationType = std::variant<CanonicalType, ConstructionType>;

// Both construction and publication use this type rule. Dependencies are walked
// by type, never by array element, so large default arrays retain compact semantics.
template<typename ReadType, typename ReadDeclaration>
auto query_default_initialization(
    ConstructionTypeRef type,
    ReadType read_type,
    ReadDeclaration read_declaration
) noexcept -> DefaultInitialization {
    auto pending = std::vector<ConstructionTypeRef> {type};
    auto visited = std::set<ConstructionTypeRef>();
    auto result = DefaultInitialization::Value;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto supported = read_type(current).visit([&](const auto& shape) noexcept {
            const auto inspect = [&](const auto& value) noexcept -> bool {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TypeID>) {
                    pending.push_back(value);
                    return true;
                } else if constexpr (std::same_as<Value, BuiltinTypeValue>) {
                    return builtin_is_numeric(value.kind)
                        || value.kind == BuiltinType::U8x32
                        || value.kind == BuiltinType::Mask32
                        || value.kind == BuiltinType::F32x8
                        || value.kind == BuiltinType::Mask8
                        || value.kind == BuiltinType::U8x16
                        || value.kind == BuiltinType::Mask16
                        || value.kind == BuiltinType::F32x4
                        || value.kind == BuiltinType::Mask4
                        || value.kind == BuiltinType::Bool
                        || value.kind == BuiltinType::Char
                        || value.kind == BuiltinType::Str
                        || value.kind == BuiltinType::Void
                        || value.kind == BuiltinType::String;
                } else if constexpr (std::same_as<Value, TupleTypeValue>
                                     || std::same_as<Value, ConstructionTupleTypeValue>
                                     || std::same_as<Value, GenericTupleType>) {
                    for (const auto element : value.elements | std::views::reverse) {
                        pending.push_back(element);
                    }
                    return true;
                } else if constexpr (std::same_as<Value, VectorTypeValue>
                                     || std::same_as<Value, GenericVectorType>) {
                    return true;
                } else if constexpr (std::same_as<Value, ArrayTypeValue>
                                     || std::same_as<Value, ConstructionArrayTypeValue>
                                     || std::same_as<Value, GenericArrayType>) {
                    if (value.extent != 0u) {
                        pending.push_back(value.element);
                    }
                    return true;
                } else if constexpr (std::same_as<Value, StructTypeValue>
                                     || std::same_as<Value, GenericNominalApplication>) {
                    const auto append_fields = [&](const auto& declaration) noexcept {
                        if (declaration.kind == RecordKind::Class) {
                            return false;
                        }
                        for (const auto& field : declaration.fields | std::views::reverse) {
                            pending.push_back(field.type);
                        }
                        return true;
                    };
                    if constexpr (std::same_as<Value, StructTypeValue>) {
                        return append_fields(read_declaration(current, value));
                    } else {
                        const auto declaration = read_declaration(current, value);
                        return declaration && append_fields(*declaration);
                    }
                } else if constexpr (std::same_as<Value, CppTypeValue>
                                     || std::same_as<Value, GenericCppNamedType>
                                     || std::same_as<Value, GenericCppQueryType>) {
                    result = DefaultInitialization::Native;
                    return true;
                } else {
                    return std::same_as<Value, PointerTypeValue>
                        || std::same_as<Value, GenericPointerType>
                        || std::same_as<Value, SliceTypeValue>
                        || std::same_as<Value, GenericSliceType>
                        || std::same_as<Value, ConstructionSliceTypeValue>
                        || std::same_as<Value, RangeTypeValue>;
                }
            };
            if constexpr (std::same_as<
                              std::remove_cvref_t<decltype(shape)>,
                              GenericTypeExpression>) {
                return shape.visit(inspect);
            } else {
                return shape.value.visit(inspect);
            }
        });
        if (!supported) {
            return DefaultInitialization::Unavailable;
        }
    }
    return result;
}

// Referenced nominal declarations must have completed canonical field types.
auto default_initialization(const SemIRProgram& program, TypeID type) noexcept
    -> DefaultInitialization;
