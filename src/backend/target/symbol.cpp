module carven:backend.target.symbol.impl;

import :backend.target.symbol;
import std;

auto target_symbol_spelling(TargetSymbol symbol) noexcept -> std::string_view {
    switch (symbol) {
        case TargetSymbol::StdExitFailure:        return "EXIT_FAILURE";
        case TargetSymbol::Auto:                  return "auto";
        case TargetSymbol::DecltypeAuto:          return "decltype(auto)";
        case TargetSymbol::Void:                  return "void";
        case TargetSymbol::Bool:                  return "bool";
        case TargetSymbol::Char:                  return "char32_t";
        case TargetSymbol::Int:                   return "int";
        case TargetSymbol::CChar:                 return "char";
        case TargetSymbol::StdInt8:               return "std::int8_t";
        case TargetSymbol::StdInt16:              return "std::int16_t";
        case TargetSymbol::StdInt32:              return "std::int32_t";
        case TargetSymbol::StdInt64:              return "std::int64_t";
        case TargetSymbol::StdUInt8:              return "std::uint8_t";
        case TargetSymbol::StdUInt16:             return "std::uint16_t";
        case TargetSymbol::StdUInt32:             return "std::uint32_t";
        case TargetSymbol::StdUInt64:             return "std::uint64_t";
        case TargetSymbol::StdPtrdiff:            return "std::ptrdiff_t";
        case TargetSymbol::StdSize:               return "std::size_t";
        case TargetSymbol::Float:                 return "float";
        case TargetSymbol::Double:                return "double";
        case TargetSymbol::RuntimeDeferredResult: return "carven::runtime::DeferredResult";
        case TargetSymbol::RuntimeReadArg:        return "carven::runtime::ReadArg";
        case TargetSymbol::RuntimeTransfer:       return "carven::runtime::transfer";
        case TargetSymbol::RuntimeUnwrapNativeResult:
            return "carven::runtime::unwrap_native_result";
        case TargetSymbol::RuntimeTestStopped:       return "carven::runtime::TestStopped";
        case TargetSymbol::RuntimeCurrentTest:       return "carven::runtime::current_test";
        case TargetSymbol::RuntimeOutcome:           return "carven::runtime::Outcome";
        case TargetSymbol::RuntimePrint:             return "carven::runtime::print";
        case TargetSymbol::RuntimePrintln:           return "carven::runtime::println";
        case TargetSymbol::RuntimeEprint:            return "carven::runtime::eprint";
        case TargetSymbol::RuntimeEprintln:          return "carven::runtime::eprintln";
        case TargetSymbol::RuntimeFormat:            return "carven::runtime::format";
        case TargetSymbol::RuntimeAsSlice:           return "carven::runtime::as_slice";
        case TargetSymbol::RuntimeSlice:             return "carven::runtime::Slice";
        case TargetSymbol::RuntimeString:            return "carven::runtime::String";
        case TargetSymbol::RuntimeStrCharsView:      return "carven::runtime::StrCharsView";
        case TargetSymbol::RuntimeEntryArgs:         return "carven::runtime::entry_args";
        case TargetSymbol::RuntimeFunctionRef:       return "carven::runtime::FunctionRef";
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
        case TargetSymbol::RuntimeCheckedArrayIndex: return "carven::runtime::checked_array_index";
        case TargetSymbol::RuntimeUTF8Text:          return "carven::runtime::utf8_text";
        case TargetSymbol::RuntimeCheckedUnicodeScalar:
            return "carven::runtime::checked_unicode_scalar";
        case TargetSymbol::StdRemoveCVRef:      return "std::remove_cvref_t";
        case TargetSymbol::StdAddConst:         return "std::add_const_t";
        case TargetSymbol::StdTypeIdentity:     return "std::type_identity_t";
        case TargetSymbol::StdReferenceWrapper: return "std::reference_wrapper";
        case TargetSymbol::StdAddressof:        return "std::addressof";
        case TargetSymbol::StdGetIf:            return "std::get_if";
        case TargetSymbol::StdDeclval:          return "::std::declval";
        case TargetSymbol::StdForward:          return "std::forward";
        case TargetSymbol::StdMove:             return "std::move";
        case TargetSymbol::StdNullopt:          return "std::nullopt";
        case TargetSymbol::StdNullptr:          return "nullptr";
        case TargetSymbol::StdOptional:         return "std::optional";
        case TargetSymbol::StdStringView:       return "std::string_view";
        case TargetSymbol::StdVariant:          return "std::variant";
        case TargetSymbol::TestingContext:      return "carven::runtime::TestContext";
        case TargetSymbol::TestingReporter:     return "carven::runtime::TestReporter";
    }
    std::unreachable();
}
