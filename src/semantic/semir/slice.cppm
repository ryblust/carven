module carven:semantic.semir.slice;

import :semantic.semir.type;
import std;

enum class SliceIntrinsic { FromOwner, Length, IsEmpty, Slice };
enum class SliceIntrinsicShape { Array, Slice, Vector, ArrayOrVector, ArrayOrSlice };

struct SliceOfReceiver final {};

using SliceIntrinsicResult = std::variant<BuiltinType, SliceOfReceiver>;

struct SliceIntrinsicContract final {
    SliceIntrinsicShape receiver;
    std::span<const BuiltinType> arguments;
    SliceIntrinsicResult result;
};

constexpr auto slice_intrinsic_accepts_receiver(
    SliceIntrinsicShape requirement,
    SliceIntrinsicShape actual
) noexcept -> bool {
    if (requirement == SliceIntrinsicShape::ArrayOrVector) {
        return actual == SliceIntrinsicShape::Array || actual == SliceIntrinsicShape::Vector;
    }
    return requirement == SliceIntrinsicShape::ArrayOrSlice
        ? actual == SliceIntrinsicShape::Array || actual == SliceIntrinsicShape::Slice
        : requirement == actual;
}

// All slice operands are Read. A slice result retains the receiver's element type.
constexpr auto slice_intrinsic_contract(SliceIntrinsic intrinsic) noexcept
    -> SliceIntrinsicContract {
    static constexpr auto bounds = std::array {BuiltinType::Usize, BuiltinType::Usize};
    switch (intrinsic) {
        case SliceIntrinsic::FromOwner:
            return {SliceIntrinsicShape::ArrayOrVector, {}, SliceOfReceiver {}};
        case SliceIntrinsic::Length:
            return {SliceIntrinsicShape::ArrayOrSlice, {}, BuiltinType::Usize};
        case SliceIntrinsic::IsEmpty:
            return {SliceIntrinsicShape::ArrayOrSlice, {}, BuiltinType::Bool};
        case SliceIntrinsic::Slice: return {SliceIntrinsicShape::Slice, bounds, SliceOfReceiver {}};
    }
    std::unreachable();
}
