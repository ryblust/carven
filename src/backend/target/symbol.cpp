module carven:backend.target.symbol.impl;

import :backend.target.symbol;
import std;

namespace {

auto symbol_info(
    std::string_view spelling,
    std::string_view header = {},
    bool allows_implicit_discard = false
) noexcept -> TargetSymbolInfo {
    return {
        .spelling = spelling,
        .header = header,
        .allows_implicit_discard = allows_implicit_discard
    };
}

} // namespace

auto target_symbol_info(TargetSymbol symbol) noexcept -> TargetSymbolInfo {
    switch (symbol) {
        case TargetSymbol::StdExitFailure: return symbol_info("EXIT_FAILURE", "cstdlib");
        case TargetSymbol::Auto:           return symbol_info("auto");
        case TargetSymbol::DecltypeAuto:   return symbol_info("decltype(auto)");
        case TargetSymbol::Void:           return symbol_info("void");
        case TargetSymbol::Bool:           return symbol_info("bool");
        case TargetSymbol::Char:           return symbol_info("char32_t");
        case TargetSymbol::Int:            return symbol_info("int");
        case TargetSymbol::CChar:          return symbol_info("char");
        case TargetSymbol::StdInt8:        return symbol_info("::std::int8_t", "cstdint");
        case TargetSymbol::StdInt16:       return symbol_info("::std::int16_t", "cstdint");
        case TargetSymbol::StdInt32:       return symbol_info("::std::int32_t", "cstdint");
        case TargetSymbol::StdInt64:       return symbol_info("::std::int64_t", "cstdint");
        case TargetSymbol::StdUInt8:       return symbol_info("::std::uint8_t", "cstdint");
        case TargetSymbol::StdUInt16:      return symbol_info("::std::uint16_t", "cstdint");
        case TargetSymbol::StdUInt32:      return symbol_info("::std::uint32_t", "cstdint");
        case TargetSymbol::StdUInt64:      return symbol_info("::std::uint64_t", "cstdint");
        case TargetSymbol::StdPtrdiff:     return symbol_info("::std::ptrdiff_t", "cstddef");
        case TargetSymbol::StdSize:        return symbol_info("::std::size_t", "cstddef");
        case TargetSymbol::Float:          return symbol_info("float");
        case TargetSymbol::Double:         return symbol_info("double");
        case TargetSymbol::RuntimeDeferredResult:
            return symbol_info("::carven::runtime::DeferredResult", "carven/runtime/deferred.hpp");
        case TargetSymbol::RuntimeReadArg:
            return symbol_info("::carven::runtime::ReadArg", "carven/runtime/passing.hpp");
        case TargetSymbol::RuntimeTransfer:
            return symbol_info("::carven::runtime::transfer", "carven/runtime/passing.hpp", true);
        case TargetSymbol::RuntimeNativeTestResult:
            return symbol_info(
                "::carven::runtime::native_test_result",
                "carven/runtime/testing.hpp",
                true
            );
        case TargetSymbol::RuntimeTestStopped:
            return symbol_info("::carven::runtime::TestStopped", "carven/runtime/testing.hpp");
        case TargetSymbol::RuntimeAssertionFailed:
            return symbol_info(
                "::carven::runtime::assertion_failed",
                "carven/runtime/report.hpp",
                true
            );
        case TargetSymbol::RuntimeCurrentTest:
            return symbol_info(
                "::carven::runtime::current_test",
                "carven/runtime/testing.hpp",
                true
            );
        case TargetSymbol::RuntimeOutcome:
            return symbol_info("::carven::runtime::Outcome", "carven/runtime/outcome.hpp");
        case TargetSymbol::RuntimeObserveComparison:
            return symbol_info(
                "::carven::runtime::observe_comparison",
                "carven/runtime/report.hpp"
            );
        case TargetSymbol::RuntimeObserveShortCircuit:
            return symbol_info(
                "::carven::runtime::observe_short_circuit",
                "carven/runtime/report.hpp"
            );
        case TargetSymbol::RuntimeDisplayWriter:
            return symbol_info("::carven::runtime::DisplayWriter", "carven/runtime/display.hpp");
        case TargetSymbol::RuntimeStructuralDisplay:
            return symbol_info(
                "::carven::runtime::structural_display",
                "carven/runtime/display.hpp"
            );
        case TargetSymbol::RuntimePrint:
            return symbol_info("::carven::runtime::print", "carven/runtime/print.hpp", true);
        case TargetSymbol::RuntimePrintln:
            return symbol_info("::carven::runtime::println", "carven/runtime/print.hpp", true);
        case TargetSymbol::RuntimeEprint:
            return symbol_info("::carven::runtime::eprint", "carven/runtime/print.hpp", true);
        case TargetSymbol::RuntimeEprintln:
            return symbol_info("::carven::runtime::eprintln", "carven/runtime/print.hpp", true);
        case TargetSymbol::RuntimeFormat:
            return symbol_info("::carven::runtime::format", "carven/runtime/format.hpp", true);
        case TargetSymbol::RuntimeFormatValidUTF8:
            return symbol_info(
                "::carven::runtime::format_valid_utf8",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeAppendFormat:
            return symbol_info(
                "::carven::runtime::append_format",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeAppendFormatValidUTF8:
            return symbol_info(
                "::carven::runtime::append_format_valid_utf8",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeAdoptArray:
            return symbol_info("::carven::runtime::adopt_array", "carven/runtime/array.hpp", true);
        case TargetSymbol::RuntimeAsSlice:
            return symbol_info("::carven::runtime::as_slice", "carven/runtime/slice.hpp", true);
        case TargetSymbol::RuntimeRange:
            return symbol_info("::carven::runtime::Range", "carven/runtime/range.hpp");
        case TargetSymbol::RuntimeSlice:
            return symbol_info("::carven::runtime::Slice", "carven/runtime/slice.hpp");
        case TargetSymbol::RuntimeString:
            return symbol_info("::carven::runtime::String", "carven/runtime/string.hpp");
        case TargetSymbol::RuntimeStrCharsView:
            return symbol_info("::carven::runtime::StrCharsView", "carven/runtime/text.hpp");
        case TargetSymbol::RuntimeEntryArgsType:
            return symbol_info("::carven::runtime::EntryArgs", "carven/runtime/entry.hpp");
        case TargetSymbol::RuntimeEntryArgs:
            return symbol_info("::carven::runtime::entry_args", "carven/runtime/entry.hpp", true);
        case TargetSymbol::RuntimeFunctionRef:
            return symbol_info("::carven::runtime::FunctionRef", "carven/runtime/callable.hpp");
        case TargetSymbol::RuntimeTextBytes:
            return symbol_info("::carven::runtime::text_bytes", "carven/runtime/text.hpp", true);
        case TargetSymbol::RuntimeTextChars:
            return symbol_info("::carven::runtime::text_chars", "carven/runtime/text.hpp", true);
        case TargetSymbol::RuntimeIntegerNegate:
            return symbol_info(
                "::carven::runtime::integer_negate",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerAdd:
            return symbol_info(
                "::carven::runtime::integer_add",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerSubtract:
            return symbol_info(
                "::carven::runtime::integer_subtract",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerMultiply:
            return symbol_info(
                "::carven::runtime::integer_multiply",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerDivide:
            return symbol_info(
                "::carven::runtime::integer_divide",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeWriter:
            return symbol_info("::carven::runtime::Writer", "carven/runtime/writer.hpp");
        case TargetSymbol::RuntimeIntegerRemainder:
            return symbol_info(
                "::carven::runtime::integer_remainder",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerLeftShift:
            return symbol_info(
                "::carven::runtime::integer_left_shift",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerRightShift:
            return symbol_info(
                "::carven::runtime::integer_right_shift",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeCheckedArrayIndex:
            return symbol_info(
                "::carven::runtime::checked_array_index",
                "carven/runtime/array.hpp",
                true
            );
        case TargetSymbol::RuntimeUTF8Text:
            return symbol_info("::carven::runtime::utf8_text", "carven/runtime/text.hpp", true);
        case TargetSymbol::RuntimeCheckedUnicodeScalar:
            return symbol_info(
                "::carven::runtime::checked_unicode_scalar",
                "carven/runtime/text.hpp",
                true
            );
        case TargetSymbol::StdRemoveCVRef:
            return symbol_info("::std::remove_cvref_t", "type_traits");
        case TargetSymbol::StdAddConst: return symbol_info("::std::add_const_t", "type_traits");
        case TargetSymbol::StdTypeIdentity:
            return symbol_info("::std::type_identity_t", "type_traits");
        case TargetSymbol::StdReferenceWrapper:
            return symbol_info("::std::reference_wrapper", "functional");
        case TargetSymbol::StdAddressof: return symbol_info("::std::addressof", "memory");
        case TargetSymbol::StdGetIf:     return symbol_info("::std::get_if", "variant");
        case TargetSymbol::StdDeclval:   return symbol_info("::std::declval", "utility");
        case TargetSymbol::StdForward:   return symbol_info("::std::forward", "utility");
        case TargetSymbol::StdBitCast:   return symbol_info("::std::bit_cast", "bit");
        case TargetSymbol::StdMove:      return symbol_info("::std::move", "utility");
        case TargetSymbol::StdAsConst:   return symbol_info("::std::as_const", "utility");
        case TargetSymbol::StdNullopt:   return symbol_info("::std::nullopt", "optional");
        case TargetSymbol::StdNullptr:   return symbol_info("nullptr");
        case TargetSymbol::StdInitializerList:
            return symbol_info("::std::initializer_list", "initializer_list");
        case TargetSymbol::StdOptional:   return symbol_info("::std::optional", "optional");
        case TargetSymbol::StdStringView: return symbol_info("::std::string_view", "string_view");
        case TargetSymbol::StdVariant:    return symbol_info("::std::variant", "variant");
        case TargetSymbol::TestingContext:
            return symbol_info("::carven::runtime::TestContext", "carven/runtime/testing.hpp");
        case TargetSymbol::TestingReporter:
            return symbol_info("::carven::runtime::TestReporter", "carven/runtime/testing.hpp");
    }
    std::unreachable();
}
