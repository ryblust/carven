module carven:semantic.semir.sequence;

import :semantic.semir.constant_access;
import :semantic.semir.generic;
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

// Queried after referenced nominal heads have completed.
auto unsupported_sequence_element(const ExecutionValueAccess& values, TypeID element) noexcept
    -> std::optional<std::string_view>;

// Concrete sequence storage has source-defined value semantics and no
// contained loans. Rigid parameters defer this rule to their concrete use.
template<typename Shape>
auto unsupported_sequence_element_shape(const Shape& value) noexcept
    -> std::optional<std::string_view> {
    if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
        if (value.kind == BuiltinType::Str || value.kind == BuiltinType::StrCharsView) {
            return "Sequence elements cannot contain borrowed text";
        }
        if (value.kind == BuiltinType::Void || value.kind == BuiltinType::EntryArgs) {
            return "Sequence element is not an owning value type";
        }
    } else if constexpr (std::same_as<Shape, SliceTypeValue>
                         || std::same_as<Shape, GenericSliceType>) {
        return "Sequence elements cannot contain borrowed slices";
    } else if constexpr (std::same_as<Shape, CppTypeValue>) {
        return "Sequence elements require Carven-defined value semantics";
    } else if constexpr (std::same_as<Shape, ClosureTypeValue>
                         || std::same_as<Shape, FunctionTypeValue>
                         || std::same_as<Shape, CallableViewTypeValue>) {
        return "Sequence elements cannot contain callable values";
    }
    return std::nullopt;
}
