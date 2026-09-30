module carven:semantic.semir.simd.impl;

import :semantic.semir.simd;
import :support.invariant;
import std;

auto simd_layout(BuiltinType owner) noexcept -> std::optional<SIMDLayout> {
    switch (owner) {
        case BuiltinType::U8x16:
        case BuiltinType::Mask16:
            return SIMDLayout {
                .vector = BuiltinType::U8x16,
                .mask = BuiltinType::Mask16,
                .element = BuiltinType::U8,
                .bits = BuiltinType::U16,
                .width = 16
            };
        case BuiltinType::U8x32:
        case BuiltinType::Mask32:
            return SIMDLayout {
                .vector = BuiltinType::U8x32,
                .mask = BuiltinType::Mask32,
                .element = BuiltinType::U8,
                .bits = BuiltinType::U32,
                .width = 32
            };
        case BuiltinType::F32x4:
        case BuiltinType::Mask4:
            return SIMDLayout {
                .vector = BuiltinType::F32x4,
                .mask = BuiltinType::Mask4,
                .element = BuiltinType::F32,
                .bits = BuiltinType::U8,
                .width = 4
            };
        case BuiltinType::F32x8:
        case BuiltinType::Mask8:
            return SIMDLayout {
                .vector = BuiltinType::F32x8,
                .mask = BuiltinType::Mask8,
                .element = BuiltinType::F32,
                .bits = BuiltinType::U8,
                .width = 8
            };
        default: return std::nullopt;
    }
}

namespace {

enum class SIMDSlot { Vector, Mask, Element, Bits, Index, Boolean, Array, Slice };

struct IntrinsicContract final {
    SIMDIntrinsic intrinsic;
    std::string_view name;
    bool factory;
    bool bytes_only;
    std::array<SIMDSlot, 3> inputs;
    std::size_t arity;
    SIMDSlot result;
    std::optional<std::size_t> static_input;
};

inline constexpr auto contracts = std::array {
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Splat,
        .name = "splat",
        .factory = true,
        .bytes_only = false,
        .inputs = {SIMDSlot::Element},
        .arity = 1uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::FromArray,
        .name = "from_array",
        .factory = true,
        .bytes_only = false,
        .inputs = {SIMDSlot::Array},
        .arity = 1uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::ToArray,
        .name = "to_array",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Vector},
        .arity = 1uz,
        .result = SIMDSlot::Array,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Load,
        .name = "load",
        .factory = true,
        .bytes_only = false,
        .inputs = {SIMDSlot::Slice, SIMDSlot::Index},
        .arity = 2uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::LoadPartial,
        .name = "load_partial",
        .factory = true,
        .bytes_only = false,
        .inputs = {SIMDSlot::Slice, SIMDSlot::Index, SIMDSlot::Element},
        .arity = 3uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Lane,
        .name = "lane",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Vector, SIMDSlot::Index},
        .arity = 2uz,
        .result = SIMDSlot::Element,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::WithLane,
        .name = "with_lane",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Vector, SIMDSlot::Index, SIMDSlot::Element},
        .arity = 3uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Lookup,
        .name = "lookup",
        .factory = false,
        .bytes_only = true,
        .inputs = {SIMDSlot::Vector, SIMDSlot::Vector},
        .arity = 2uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Extract,
        .name = "extract",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Vector, SIMDSlot::Vector, SIMDSlot::Index},
        .arity = 3uz,
        .result = SIMDSlot::Vector,
        .static_input = 2uz
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::ShiftLeft,
        .name = "shift_left",
        .factory = false,
        .bytes_only = true,
        .inputs = {SIMDSlot::Vector, SIMDSlot::Index},
        .arity = 2uz,
        .result = SIMDSlot::Vector,
        .static_input = 1uz
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::ShiftRight,
        .name = "shift_right",
        .factory = false,
        .bytes_only = true,
        .inputs = {SIMDSlot::Vector, SIMDSlot::Index},
        .arity = 2uz,
        .result = SIMDSlot::Vector,
        .static_input = 1uz
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::FromBits,
        .name = "from_bits",
        .factory = true,
        .bytes_only = false,
        .inputs = {SIMDSlot::Bits},
        .arity = 1uz,
        .result = SIMDSlot::Mask,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Prefix,
        .name = "prefix",
        .factory = true,
        .bytes_only = false,
        .inputs = {SIMDSlot::Index},
        .arity = 1uz,
        .result = SIMDSlot::Mask,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Bits,
        .name = "bits",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Mask},
        .arity = 1uz,
        .result = SIMDSlot::Bits,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Any,
        .name = "any",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Mask},
        .arity = 1uz,
        .result = SIMDSlot::Boolean,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::All,
        .name = "all",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Mask},
        .arity = 1uz,
        .result = SIMDSlot::Boolean,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Count,
        .name = "count",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Mask},
        .arity = 1uz,
        .result = SIMDSlot::Index,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::FirstOr,
        .name = "first_or",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Mask, SIMDSlot::Index},
        .arity = 2uz,
        .result = SIMDSlot::Index,
        .static_input = std::nullopt
    },
    IntrinsicContract {
        .intrinsic = SIMDIntrinsic::Select,
        .name = "select",
        .factory = false,
        .bytes_only = false,
        .inputs = {SIMDSlot::Mask, SIMDSlot::Vector, SIMDSlot::Vector},
        .arity = 3uz,
        .result = SIMDSlot::Vector,
        .static_input = std::nullopt
    }
};

auto contract_for(SIMDIntrinsic intrinsic) noexcept -> const IntrinsicContract& {
    const auto index = static_cast<std::size_t>(intrinsic);
    if (index >= contracts.size()) {
        invariant_violation("invalid SIMD intrinsic");
    }
    return contracts[index];
}

} // namespace

auto simd_is_factory(SIMDIntrinsic intrinsic) noexcept -> bool {
    return contract_for(intrinsic).factory;
}

auto simd_reports(SIMDIntrinsic intrinsic) noexcept -> bool {
    switch (intrinsic) {
        case SIMDIntrinsic::Load:
        case SIMDIntrinsic::LoadPartial:
        case SIMDIntrinsic::Lane:
        case SIMDIntrinsic::WithLane:
        case SIMDIntrinsic::Prefix:      return true;
        default:                         return false;
    }
}

auto simd_static_input(SIMDIntrinsic intrinsic) noexcept -> std::optional<std::size_t> {
    return contract_for(intrinsic).static_input;
}

auto simd_static_limit(SIMDIntrinsic intrinsic, BuiltinType owner) noexcept -> std::uint64_t {
    return intrinsic == SIMDIntrinsic::Extract ? simd_layout(owner)->width + 1u : 8u;
}

auto simd_contract(SIMDIntrinsic intrinsic, BuiltinType owner) noexcept -> SIMDContract {
    const auto layout = simd_layout(owner);
    if (!layout) {
        invariant_violation("invalid SIMD owner");
    }
    const auto slot = [&](SIMDSlot kind) noexcept -> SIMDType {
        switch (kind) {
            case SIMDSlot::Vector:  return layout->vector;
            case SIMDSlot::Mask:    return layout->mask;
            case SIMDSlot::Element: return layout->element;
            case SIMDSlot::Bits:    return layout->bits;
            case SIMDSlot::Index:   return BuiltinType::Usize;
            case SIMDSlot::Boolean: return BuiltinType::Bool;
            case SIMDSlot::Array:
                return SIMDShape {.element = layout->element, .extent = layout->width};
            case SIMDSlot::Slice:
                return SIMDShape {.element = layout->element, .extent = std::nullopt};
        }
        std::unreachable();
    };
    const auto& contract = contract_for(intrinsic);
    auto inputs = std::vector<SIMDType>();
    auto stages = std::vector<ParameterStage>(contract.arity, ParameterStage::Runtime);
    for (auto i = 0uz; i < contract.arity; ++i) {
        inputs.push_back(slot(contract.inputs[i]));
    }
    if (contract.static_input) {
        stages[*contract.static_input] = ParameterStage::Static;
    }
    return {
        .inputs = std::move(inputs),
        .result = slot(contract.result),
        .name = contract.name,
        .stages = std::move(stages)
    };
}

auto simd_member(BuiltinType type, std::string_view name, bool factory) noexcept
    -> std::optional<SIMDIntrinsic> {
    const auto layout = simd_layout(type);
    if (!layout) {
        return std::nullopt;
    }
    for (const auto& contract : contracts) {
        if (contract.bytes_only && layout->element != BuiltinType::U8) {
            continue;
        }
        const auto receiver = contract.factory ? contract.result : contract.inputs.front();
        const auto owner = receiver == SIMDSlot::Mask ? layout->mask : layout->vector;
        if (owner == type && contract.name == name && contract.factory == factory) {
            return contract.intrinsic;
        }
    }
    return std::nullopt;
}
