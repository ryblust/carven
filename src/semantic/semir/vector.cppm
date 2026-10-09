module carven:semantic.semir.vector;

import :semantic.semir.type;
import std;

enum class VectorIntrinsic {
    Length,
    IsEmpty,
    Push,
    Remove,
    Clear,
    Capacity,
    Reserve,
    FromSlice,
    WithCapacity
};
enum class VectorIntrinsicArgument { Element, Index, Capacity, Slice };
enum class VectorIntrinsicResult { Element, Owner };

struct VectorIntrinsicOperation final {
    VectorIntrinsic intrinsic;
};

struct VectorIntrinsicContract final {
    AccessMode receiver_access;
    std::optional<VectorIntrinsicArgument> argument;
    std::variant<BuiltinType, VectorIntrinsicResult> result;
};

constexpr auto vector_intrinsic_contract(VectorIntrinsic intrinsic) noexcept
    -> VectorIntrinsicContract {
    switch (intrinsic) {
        case VectorIntrinsic::Capacity:
        case VectorIntrinsic::Length:   return {AccessMode::Read, std::nullopt, BuiltinType::Usize};
        case VectorIntrinsic::IsEmpty:  return {AccessMode::Read, std::nullopt, BuiltinType::Bool};
        case VectorIntrinsic::Push:
            return {AccessMode::Write, VectorIntrinsicArgument::Element, BuiltinType::Void};
        case VectorIntrinsic::Remove:
            return {
                AccessMode::Write,
                VectorIntrinsicArgument::Index,
                VectorIntrinsicResult::Element
            };
        case VectorIntrinsic::Reserve:
            return {AccessMode::Write, VectorIntrinsicArgument::Capacity, BuiltinType::Void};
        case VectorIntrinsic::FromSlice:
            return {AccessMode::Read, VectorIntrinsicArgument::Slice, VectorIntrinsicResult::Owner};
        case VectorIntrinsic::WithCapacity:
            return {
                AccessMode::Read,
                VectorIntrinsicArgument::Capacity,
                VectorIntrinsicResult::Owner
            };
        case VectorIntrinsic::Clear: return {AccessMode::Write, std::nullopt, BuiltinType::Void};
    }
    std::unreachable();
}

constexpr auto vector_intrinsic_factory(VectorIntrinsic intrinsic) noexcept -> bool {
    return vector_intrinsic_contract(intrinsic).result
        == decltype(VectorIntrinsicContract::result) {VectorIntrinsicResult::Owner};
}

template<typename Type, typename Builtin>
auto vector_intrinsic_result_type(
    VectorIntrinsic intrinsic,
    Type owner,
    Type element,
    Builtin builtin
) noexcept -> Type {
    const auto result = vector_intrinsic_contract(intrinsic).result;
    if (const auto* scalar = std::get_if<BuiltinType>(&result)) {
        return builtin(*scalar);
    }
    return std::get<VectorIntrinsicResult>(result) == VectorIntrinsicResult::Element ? element
                                                                                     : owner;
}
