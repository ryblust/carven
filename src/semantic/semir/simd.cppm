module carven:semantic.semir.simd;

import :semantic.semir.type;
import std;

enum class SIMDIntrinsic {
    Splat,
    FromArray,
    ToArray,
    Load,
    LoadPartial,
    Lane,
    WithLane,
    Lookup,
    Extract,
    ShiftLeft,
    ShiftRight,
    FromBits,
    Prefix,
    Bits,
    Any,
    All,
    Count,
    FirstOr,
    Select,
    Sum,
};

struct SIMDShape final {
    BuiltinType element;
    std::optional<std::uint64_t> extent;
};

struct SIMDLayout final {
    BuiltinType vector;
    BuiltinType mask;
    BuiltinType element;
    BuiltinType bits;
    std::uint64_t width;
};

auto simd_layout(BuiltinType owner) noexcept -> std::optional<SIMDLayout>;

using SIMDType = std::variant<BuiltinType, SIMDShape>;

struct SIMDContract final {
    std::vector<SIMDType> inputs;
    SIMDType result;
    std::string_view name;
    std::vector<ParameterStage> stages;
};

auto simd_is_factory(SIMDIntrinsic intrinsic) noexcept -> bool;
// A reporting operation checks an index or a memory range when it executes.
auto simd_reports(SIMDIntrinsic intrinsic) noexcept -> bool;
auto simd_static_input(SIMDIntrinsic intrinsic) noexcept -> std::optional<std::size_t>;
auto simd_static_limit(SIMDIntrinsic intrinsic, BuiltinType owner) noexcept -> std::uint64_t;

auto simd_contract(SIMDIntrinsic intrinsic, BuiltinType owner) noexcept -> SIMDContract;
auto simd_member(BuiltinType type, std::string_view name, bool factory) noexcept
    -> std::optional<SIMDIntrinsic>;

template<typename Types>
auto resolve_simd_type(Types& types, SIMDType type) noexcept -> TypeID {
    if (const auto* builtin = std::get_if<BuiltinType>(&type)) {
        return types.builtin_type(*builtin);
    }
    const auto shape = std::get<SIMDShape>(type);
    const auto element = types.builtin_type(shape.element);
    if (shape.extent) {
        return types.intern_type(
            {.value = ArrayTypeValue {.element = element, .extent = *shape.extent}}
        );
    }
    return types.intern_type({.value = SliceTypeValue {.element = element}});
}

template<typename Lookup>
auto matches_simd_type(SIMDType expected, TypeID actual, Lookup lookup) noexcept -> bool {
    const auto type = lookup(actual);
    if (const auto* builtin = std::get_if<BuiltinType>(&expected)) {
        return type.value == CanonicalTypeValue {BuiltinTypeValue {.kind = *builtin}};
    }
    const auto shape = std::get<SIMDShape>(expected);
    const auto element = CanonicalTypeValue {BuiltinTypeValue {.kind = shape.element}};
    if (shape.extent) {
        const auto* array = std::get_if<ArrayTypeValue>(&type.value);
        return array && array->extent == *shape.extent && lookup(array->element).value == element;
    }
    const auto* slice = std::get_if<SliceTypeValue>(&type.value);
    return slice && lookup(slice->element).value == element;
}

template<typename Lookup>
auto simd_owner(SIMDIntrinsic intrinsic, TypeID result, TypeID first, Lookup lookup) noexcept
    -> BuiltinType {
    return std::get<BuiltinTypeValue>(lookup(simd_is_factory(intrinsic) ? result : first).value)
        .kind;
}
