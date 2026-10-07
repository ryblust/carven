module carven:semantic.semir.sequence;

import :semantic.semir.type;
import std;

enum class SequenceIntrinsic { Len, IsEmpty, Push, Remove, Clear };
enum class SequenceIntrinsicArgument { Element, Index };

struct SequenceIntrinsicOperation final {
    SequenceIntrinsic intrinsic;
};

struct SequenceIntrinsicContract final {
    AccessMode receiver_access;
    std::optional<SequenceIntrinsicArgument> argument;
    BuiltinType result;
};

constexpr auto sequence_intrinsic_contract(SequenceIntrinsic intrinsic) noexcept
    -> SequenceIntrinsicContract {
    switch (intrinsic) {
        case SequenceIntrinsic::Len: return {AccessMode::Read, std::nullopt, BuiltinType::Usize};
        case SequenceIntrinsic::IsEmpty: return {AccessMode::Read, std::nullopt, BuiltinType::Bool};
        case SequenceIntrinsic::Push:
            return {AccessMode::Write, SequenceIntrinsicArgument::Element, BuiltinType::Void};
        case SequenceIntrinsic::Remove:
            return {AccessMode::Write, SequenceIntrinsicArgument::Index, BuiltinType::Void};
        case SequenceIntrinsic::Clear: return {AccessMode::Write, std::nullopt, BuiltinType::Void};
    }
    std::unreachable();
}
