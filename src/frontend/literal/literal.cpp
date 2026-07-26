module carven:frontend.literal.impl;

import :frontend.literal;
import std;

auto numeric_suffix(const NumericLiteralValue& value) noexcept -> NumericSuffix {
    if (const auto* integer = std::get_if<IntegerLiteralValue>(&value)) {
        return integer->suffix;
    }
    return std::get<FloatingLiteralValue>(value).suffix;
}

auto std::formatter<NumericSuffix>::display_name(NumericSuffix suffix) noexcept
    -> std::string_view {
    switch (suffix) {
        using enum NumericSuffix;
        case None:  return "None";
        case I8:    return "I8";
        case I16:   return "I16";
        case I32:   return "I32";
        case I64:   return "I64";
        case U8:    return "U8";
        case U16:   return "U16";
        case U32:   return "U32";
        case U64:   return "U64";
        case Isize: return "Isize";
        case Usize: return "Usize";
        case F32:   return "F32";
        case F64:   return "F64";
    }
    std::unreachable();
}
