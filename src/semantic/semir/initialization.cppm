module carven:semantic.semir.initialization;

import :semantic.semir.constant_access;
import :semantic.semir.type;
import std;

enum class DefaultInitialization { Unavailable, Value, Native };

using InitializationType = std::variant<CanonicalType, ConstructionType>;

// Both construction and publication use this type rule. Dependencies are walked
// by type, never by array element, so large default arrays retain compact semantics.
template<typename ReadType, typename ReadFields>
auto query_default_initialization(
    ConstructionTypeRef type,
    ReadType read_type,
    ReadFields read_fields
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
            return shape.value.visit([&](const auto& value) noexcept -> bool {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, BuiltinTypeValue>) {
                    return builtin_is_numeric(value.kind)
                        || value.kind == BuiltinType::Bool
                        || value.kind == BuiltinType::Char
                        || value.kind == BuiltinType::Str
                        || value.kind == BuiltinType::String;
                } else if constexpr (std::same_as<Value, ArrayTypeValue>
                                     || std::same_as<Value, ConstructionArrayTypeValue>) {
                    if (value.extent != 0u) {
                        pending.push_back(value.element);
                    }
                    return true;
                } else if constexpr (std::same_as<Value, StructTypeValue>) {
                    const auto fields = read_fields(value.structure);
                    pending.insert(pending.end(), fields.rbegin(), fields.rend());
                    return true;
                } else if constexpr (std::same_as<Value, CppTypeValue>) {
                    result = DefaultInitialization::Native;
                    return true;
                } else {
                    return std::same_as<Value, PointerTypeValue>
                        || std::same_as<Value, SliceTypeValue>
                        || std::same_as<Value, ConstructionSliceTypeValue>
                        || std::same_as<Value, RangeTypeValue>;
                }
            });
        });
        if (!supported) {
            return DefaultInitialization::Unavailable;
        }
    }
    return result;
}

// Referenced nominal declarations must have completed canonical field types.
auto default_initialization(const ExecutionValueAccess& types, TypeID type) noexcept
    -> DefaultInitialization;
