module carven:semantic.semir.text;

import :semantic.semir.type;
import :support.invariant;
import std;

enum class TextIntrinsic {
    Len,
    IsEmpty,
    Bytes,
    Chars,
    FromStr,
    FromUTF8Unchecked,
    FromU32Unchecked,
    AsStr,
    Append,
    Push,
    Clear,
};

enum class TextIntrinsicShape { Text, ByteSlice };
using TextIntrinsicType = std::variant<BuiltinType, TextIntrinsicShape>;

struct TextIntrinsicParameter final {
    AccessMode access;
    TextIntrinsicType type;
};

struct TextIntrinsicContract final {
    std::span<const TextIntrinsicParameter> parameters;
    TextIntrinsicType result;
};

constexpr auto text_intrinsic_contract(TextIntrinsic intrinsic) noexcept -> TextIntrinsicContract {
    static constexpr auto text = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Read, .type = TextIntrinsicShape::Text},
    });
    static constexpr auto string = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Read, .type = BuiltinType::String},
    });
    static constexpr auto str = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Read, .type = BuiltinType::Str},
    });
    static constexpr auto bytes = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Read, .type = TextIntrinsicShape::ByteSlice},
    });
    static constexpr auto scalar = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Read, .type = BuiltinType::U32},
    });
    static constexpr auto append = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Write, .type = BuiltinType::String},
        {.access = AccessMode::Read, .type = BuiltinType::Str},
    });
    static constexpr auto push = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Write, .type = BuiltinType::String},
        {.access = AccessMode::Read, .type = BuiltinType::Char},
    });
    static constexpr auto clear = std::to_array<TextIntrinsicParameter>({
        {.access = AccessMode::Write, .type = BuiltinType::String},
    });
    switch (intrinsic) {
        case TextIntrinsic::Len:     return {.parameters = text, .result = BuiltinType::Usize};
        case TextIntrinsic::IsEmpty: return {.parameters = text, .result = BuiltinType::Bool};
        case TextIntrinsic::Bytes:
            return {.parameters = text, .result = TextIntrinsicShape::ByteSlice};
        case TextIntrinsic::Chars: return {.parameters = text, .result = BuiltinType::StrCharsView};
        case TextIntrinsic::FromStr: return {.parameters = str, .result = BuiltinType::String};
        case TextIntrinsic::FromUTF8Unchecked:
            return {.parameters = bytes, .result = BuiltinType::Str};
        case TextIntrinsic::FromU32Unchecked:
            return {.parameters = scalar, .result = BuiltinType::Char};
        case TextIntrinsic::AsStr:  return {.parameters = string, .result = BuiltinType::Str};
        case TextIntrinsic::Append: return {.parameters = append, .result = BuiltinType::Void};
        case TextIntrinsic::Push:   return {.parameters = push, .result = BuiltinType::Void};
        case TextIntrinsic::Clear:  return {.parameters = clear, .result = BuiltinType::Void};
    }
    std::unreachable();
}

constexpr auto text_intrinsic_writes(TextIntrinsic intrinsic) noexcept -> bool {
    for (const auto& parameter : text_intrinsic_contract(intrinsic).parameters) {
        if (parameter.access == AccessMode::Write) {
            return true;
        }
    }
    return false;
}

// Resolves a concrete contract type in the caller's construction store.
template<typename Types>
auto resolve_text_intrinsic_type(Types& types, const TextIntrinsicType& type) noexcept -> TypeID {
    if (const auto* builtin = std::get_if<BuiltinType>(&type)) {
        return types.builtin_type(*builtin);
    }
    switch (std::get<TextIntrinsicShape>(type)) {
        case TextIntrinsicShape::ByteSlice:
            return types.intern_type(
                {.value = SliceTypeValue {
                     .element = types.builtin_type(BuiltinType::U8),
                 }}
            );
        case TextIntrinsicShape::Text:
            invariant_violation("text type constraint has no single contextual type");
    }
    std::unreachable();
}

template<typename Lookup>
auto matches_text_intrinsic_type(
    const TextIntrinsicType& expected,
    TypeID type,
    Lookup lookup
) noexcept -> bool {
    const auto& actual = lookup(type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&actual.value);
    if (const auto* kind = std::get_if<BuiltinType>(&expected)) {
        return builtin && builtin->kind == *kind;
    }
    switch (std::get<TextIntrinsicShape>(expected)) {
        case TextIntrinsicShape::Text:
            return builtin
                && (builtin->kind == BuiltinType::Str || builtin->kind == BuiltinType::String);
        case TextIntrinsicShape::ByteSlice:
            if (const auto* slice = std::get_if<SliceTypeValue>(&actual.value)) {
                return lookup(slice->element).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::U8}};
            }
            return false;
    }
    std::unreachable();
}
