module carven:backend.target.symbol.impl;

import :backend.target.symbol;
import std;

auto target_symbol_spelling(TargetSymbol symbol) noexcept -> std::string_view {
    switch (symbol) {
        case TargetSymbol::Auto:                     return "auto";
        case TargetSymbol::Void:                     return "void";
        case TargetSymbol::Bool:                     return "bool";
        case TargetSymbol::Char:                     return "char32_t";
        case TargetSymbol::Int:                      return "int";
        case TargetSymbol::CChar:                    return "char";
        case TargetSymbol::StdInt8:                  return "std::int8_t";
        case TargetSymbol::StdInt16:                 return "std::int16_t";
        case TargetSymbol::StdInt32:                 return "std::int32_t";
        case TargetSymbol::StdInt64:                 return "std::int64_t";
        case TargetSymbol::StdUInt8:                 return "std::uint8_t";
        case TargetSymbol::StdUInt16:                return "std::uint16_t";
        case TargetSymbol::StdUInt32:                return "std::uint32_t";
        case TargetSymbol::StdUInt64:                return "std::uint64_t";
        case TargetSymbol::StdPtrdiff:               return "std::ptrdiff_t";
        case TargetSymbol::StdSize:                  return "std::size_t";
        case TargetSymbol::Float:                    return "float";
        case TargetSymbol::Double:                   return "double";
        case TargetSymbol::RuntimeOutcome:           return "carven::runtime::Outcome";
        case TargetSymbol::RuntimeStrBytesView:      return "carven::runtime::StrBytesView";
        case TargetSymbol::RuntimeStrCharsView:      return "carven::runtime::StrCharsView";
        case TargetSymbol::RuntimeEntryArgs:         return "carven::runtime::entry_args";
        case TargetSymbol::RuntimeFunctionRef:       return "carven::runtime::FunctionRef";
        case TargetSymbol::RuntimeIs:                return "carven::runtime::is";
        case TargetSymbol::RuntimeIntegerRange:      return "carven::runtime::integer_range";
        case TargetSymbol::RuntimeStrBytes:          return "carven::runtime::str_bytes";
        case TargetSymbol::RuntimeStrChars:          return "carven::runtime::str_chars";
        case TargetSymbol::RuntimeIntegerNegate:     return "carven::runtime::integer_negate";
        case TargetSymbol::RuntimeIntegerAdd:        return "carven::runtime::integer_add";
        case TargetSymbol::RuntimeIntegerSubtract:   return "carven::runtime::integer_subtract";
        case TargetSymbol::RuntimeIntegerMultiply:   return "carven::runtime::integer_multiply";
        case TargetSymbol::RuntimeIntegerDivide:     return "carven::runtime::integer_divide";
        case TargetSymbol::RuntimeIntegerRemainder:  return "carven::runtime::integer_remainder";
        case TargetSymbol::RuntimeIntegerLeftShift:  return "carven::runtime::integer_left_shift";
        case TargetSymbol::RuntimeIntegerRightShift: return "carven::runtime::integer_right_shift";
        case TargetSymbol::RuntimeIntegerAddAssign:  return "carven::runtime::integer_add_assign";
        case TargetSymbol::RuntimeIntegerSubtractAssign:
            return "carven::runtime::integer_subtract_assign";
        case TargetSymbol::RuntimeIntegerMultiplyAssign:
            return "carven::runtime::integer_multiply_assign";
        case TargetSymbol::RuntimeIntegerDivideAssign:
            return "carven::runtime::integer_divide_assign";
        case TargetSymbol::RuntimeIntegerRemainderAssign:
            return "carven::runtime::integer_remainder_assign";
        case TargetSymbol::RuntimeIntegerLeftShiftAssign:
            return "carven::runtime::integer_left_shift_assign";
        case TargetSymbol::RuntimeIntegerRightShiftAssign:
            return "carven::runtime::integer_right_shift_assign";
        case TargetSymbol::RuntimeIntegerIncrement:  return "carven::runtime::integer_increment";
        case TargetSymbol::RuntimeIntegerDecrement:  return "carven::runtime::integer_decrement";
        case TargetSymbol::RuntimeCheckedArrayIndex: return "carven::runtime::checked_array_index";
        case TargetSymbol::RuntimeCheckedForeignStr: return "carven::runtime::checked_foreign_str";
        case TargetSymbol::RuntimeCheckedForeignChar:
            return "carven::runtime::checked_foreign_char";
        case TargetSymbol::StdAbort:             return "std::abort";
        case TargetSymbol::StdGet:               return "std::get";
        case TargetSymbol::StdHoldsAlternative:  return "std::holds_alternative";
        case TargetSymbol::StdForward:           return "std::forward";
        case TargetSymbol::StdMove:              return "std::move";
        case TargetSymbol::StdNullopt:           return "std::nullopt";
        case TargetSymbol::StdStringView:        return "std::string_view";
        case TargetSymbol::StdVariant:           return "std::variant";
        case TargetSymbol::TestingRun:           return "carven::testing::run";
        case TargetSymbol::TestingReportFailure: return "carven::testing::detail::report_failure";
        case TargetSymbol::TestingControl:       return "carven::testing::detail::TestControl";
        case TargetSymbol::TestingRegistrar:     return "carven::testing::Registrar";
    }
    std::unreachable();
}
