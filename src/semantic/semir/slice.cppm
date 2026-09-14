module carven:semantic.semir.slice;

import :semantic.semir.type;
import std;

enum class SliceIntrinsic { FromArray, Len, IsEmpty, Slice };
enum class SliceIntrinsicShape { Array, Slice };

struct SliceOfReceiver final {};

using SliceIntrinsicResult = std::variant<BuiltinType, SliceOfReceiver>;

struct SliceIntrinsicContract final {
    SliceIntrinsicShape receiver;
    std::span<const BuiltinType> arguments;
    SliceIntrinsicResult result;
};

// All slice operands are Read. A slice result retains the receiver's element type.
constexpr auto slice_intrinsic_contract(SliceIntrinsic intrinsic) noexcept
    -> SliceIntrinsicContract {
    static constexpr auto bounds = std::array {BuiltinType::Usize, BuiltinType::Usize};
    switch (intrinsic) {
        case SliceIntrinsic::FromArray: return {SliceIntrinsicShape::Array, {}, SliceOfReceiver {}};
        case SliceIntrinsic::Len:       return {SliceIntrinsicShape::Slice, {}, BuiltinType::Usize};
        case SliceIntrinsic::IsEmpty:   return {SliceIntrinsicShape::Slice, {}, BuiltinType::Bool};
        case SliceIntrinsic::Slice: return {SliceIntrinsicShape::Slice, bounds, SliceOfReceiver {}};
    }
    std::unreachable();
}
