module carven:backend.target.symbol.impl;

import :backend.target.header;
import :backend.target.symbol;
import std;

namespace {

auto symbol_info(
    std::string_view spelling,
    std::string_view header = {},
    bool allows_implicit_discard = false,
    TargetHeaderGroup group = TargetHeaderGroup::StandardLibrary
) noexcept -> TargetSymbolInfo {
    return {
        .spelling = spelling,
        .header = header.empty()
            ? std::nullopt
            : std::optional(TargetHeaderProvider {.group = group, .path = header}),
        .allows_implicit_discard = allows_implicit_discard
    };
}

auto runtime_symbol_info(
    std::string_view spelling,
    std::string_view header,
    bool allows_implicit_discard = false
) noexcept -> TargetSymbolInfo {
    return symbol_info(spelling, header, allows_implicit_discard, TargetHeaderGroup::Runtime);
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
            return runtime_symbol_info(
                "::carven::runtime::DeferredResult",
                "carven/runtime/deferred.hpp"
            );
        case TargetSymbol::RuntimeReadArg:
            return runtime_symbol_info("::carven::runtime::ReadArg", "carven/runtime/passing.hpp");
        case TargetSymbol::RuntimeTransfer:
            return runtime_symbol_info(
                "::carven::runtime::transfer",
                "carven/runtime/passing.hpp",
                true
            );
        case TargetSymbol::RuntimeNativeTestResult:
            return runtime_symbol_info(
                "::carven::runtime::native_test_result",
                "carven/runtime/testing.hpp",
                true
            );
        case TargetSymbol::RuntimeTestStopped:
            return runtime_symbol_info(
                "::carven::runtime::TestStopped",
                "carven/runtime/testing.hpp"
            );
        case TargetSymbol::RuntimeAssertionFailed:
            return runtime_symbol_info(
                "::carven::runtime::assertion_failed",
                "carven/runtime/report.hpp",
                true
            );
        case TargetSymbol::RuntimeReportTestFailure:
            return runtime_symbol_info(
                "::carven::runtime::report_test_failure",
                "carven/runtime/testing.hpp",
                true
            );
        case TargetSymbol::RuntimeOutcome:
            return runtime_symbol_info("::carven::runtime::Outcome", "carven/runtime/outcome.hpp");
        case TargetSymbol::RuntimeObserveComparison:
            return runtime_symbol_info(
                "::carven::runtime::observe_comparison",
                "carven/runtime/report.hpp"
            );
        case TargetSymbol::RuntimeObserveShortCircuit:
            return runtime_symbol_info(
                "::carven::runtime::observe_short_circuit",
                "carven/runtime/report.hpp"
            );
        case TargetSymbol::RuntimeDisplayWriter:
            return runtime_symbol_info(
                "::carven::runtime::DisplayWriter",
                "carven/runtime/display/display.hpp"
            );
        case TargetSymbol::RuntimeStatelessValue:
            return runtime_symbol_info(
                "::carven::runtime::stateless_value",
                "carven/runtime/stateless.hpp"
            );
        case TargetSymbol::RuntimeScalarDisplay:
            return runtime_symbol_info(
                "::carven::runtime::ScalarDisplay",
                "carven/runtime/display/display.hpp"
            );
        case TargetSymbol::RuntimeSequenceDisplay:
            return runtime_symbol_info(
                "::carven::runtime::SequenceDisplay",
                "carven/runtime/display/display.hpp"
            );
        case TargetSymbol::RuntimeRangeDisplay:
            return runtime_symbol_info(
                "::carven::runtime::RangeDisplay",
                "carven/runtime/display/display.hpp"
            );
        case TargetSymbol::RuntimeStructuralDisplay:
            return runtime_symbol_info(
                "::carven::runtime::structural_display",
                "carven/runtime/display/display.hpp"
            );
        case TargetSymbol::RuntimePrint:
            return runtime_symbol_info(
                "::carven::runtime::print",
                "carven/runtime/print.hpp",
                true
            );
        case TargetSymbol::RuntimePrintln:
            return runtime_symbol_info(
                "::carven::runtime::println",
                "carven/runtime/print.hpp",
                true
            );
        case TargetSymbol::RuntimeEprint:
            return runtime_symbol_info(
                "::carven::runtime::eprint",
                "carven/runtime/print.hpp",
                true
            );
        case TargetSymbol::RuntimeEprintln:
            return runtime_symbol_info(
                "::carven::runtime::eprintln",
                "carven/runtime/print.hpp",
                true
            );
        case TargetSymbol::RuntimeFormat:
            return runtime_symbol_info(
                "::carven::runtime::format",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeFormatValidUTF8:
            return runtime_symbol_info(
                "::carven::runtime::format_valid_utf8",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeAppendFormat:
            return runtime_symbol_info(
                "::carven::runtime::append_format",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeAppendFormatValidUTF8:
            return runtime_symbol_info(
                "::carven::runtime::append_format_valid_utf8",
                "carven/runtime/format.hpp",
                true
            );
        case TargetSymbol::RuntimeAdoptArray:
            return runtime_symbol_info(
                "::carven::runtime::adopt_array",
                "carven/runtime/array.hpp",
                true
            );
        case TargetSymbol::RuntimeAsSlice:
            return runtime_symbol_info(
                "::carven::runtime::as_slice",
                "carven/runtime/slice.hpp",
                true
            );
        case TargetSymbol::RuntimeRange:
            return runtime_symbol_info("::carven::runtime::Range", "carven/runtime/range.hpp");
        case TargetSymbol::RuntimeSlice:
            return runtime_symbol_info("::carven::runtime::Slice", "carven/runtime/slice.hpp");
        case TargetSymbol::RuntimeSequence:
            return runtime_symbol_info(
                "::carven::runtime::Sequence",
                "carven/runtime/sequence.hpp"
            );
        case TargetSymbol::RuntimeString:
            return runtime_symbol_info("::carven::runtime::String", "carven/runtime/text/text.hpp");
        case TargetSymbol::RuntimeF32x4:
            return runtime_symbol_info(
                "::carven::runtime::simd::F32x4",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeF32x8:
            return runtime_symbol_info(
                "::carven::runtime::simd::F32x8",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeMask4:
            return runtime_symbol_info(
                "::carven::runtime::simd::Mask4",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeMask8:
            return runtime_symbol_info(
                "::carven::runtime::simd::Mask8",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeU8x16:
            return runtime_symbol_info(
                "::carven::runtime::simd::U8x16",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeU8x32:
            return runtime_symbol_info(
                "::carven::runtime::simd::U8x32",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeMask16:
            return runtime_symbol_info(
                "::carven::runtime::simd::Mask16",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeMask32:
            return runtime_symbol_info(
                "::carven::runtime::simd::Mask32",
                "carven/runtime/simd/simd.hpp"
            );
        case TargetSymbol::RuntimeStrCharsView:
            return runtime_symbol_info(
                "::carven::runtime::StrCharsView",
                "carven/runtime/text/text.hpp"
            );
        case TargetSymbol::RuntimeEntryArgsType:
            return runtime_symbol_info("::carven::runtime::EntryArgs", "carven/runtime/entry.hpp");
        case TargetSymbol::RuntimeEntryArgs:
            return runtime_symbol_info(
                "::carven::runtime::entry_args",
                "carven/runtime/entry.hpp",
                true
            );
        case TargetSymbol::RuntimeReportEntryFailure:
            return runtime_symbol_info(
                "::carven::runtime::report_entry_failure",
                "carven/runtime/entry.hpp",
                true
            );
        case TargetSymbol::RuntimeFunctionRef:
            return runtime_symbol_info(
                "::carven::runtime::FunctionRef",
                "carven/runtime/callable.hpp"
            );
        case TargetSymbol::RuntimeTextBytes:
            return runtime_symbol_info(
                "::carven::runtime::text_bytes",
                "carven/runtime/text/text.hpp",
                true
            );
        case TargetSymbol::RuntimeTextChars:
            return runtime_symbol_info(
                "::carven::runtime::text_chars",
                "carven/runtime/text/text.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerNegate:
            return runtime_symbol_info(
                "::carven::runtime::integer_negate",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerAdd:
            return runtime_symbol_info(
                "::carven::runtime::integer_add",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerSubtract:
            return runtime_symbol_info(
                "::carven::runtime::integer_subtract",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerMultiply:
            return runtime_symbol_info(
                "::carven::runtime::integer_multiply",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerDivide:
            return runtime_symbol_info(
                "::carven::runtime::integer_divide",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeFormatWriter:
            return runtime_symbol_info(
                "::carven::runtime::FormatWriter",
                "carven/runtime/format_writer.hpp"
            );
        case TargetSymbol::RuntimeIntegerRemainder:
            return runtime_symbol_info(
                "::carven::runtime::integer_remainder",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerLeftShift:
            return runtime_symbol_info(
                "::carven::runtime::integer_left_shift",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeIntegerRightShift:
            return runtime_symbol_info(
                "::carven::runtime::integer_right_shift",
                "carven/runtime/numeric.hpp",
                true
            );
        case TargetSymbol::RuntimeCheckedArrayIndex:
            return runtime_symbol_info(
                "::carven::runtime::checked_array_index",
                "carven/runtime/array.hpp",
                true
            );
        case TargetSymbol::RuntimeCheckedSliceIndex:
            return runtime_symbol_info(
                "::carven::runtime::checked_slice_index",
                "carven/runtime/slice.hpp",
                true
            );
        case TargetSymbol::RuntimeCheckedSequenceIndex:
            return runtime_symbol_info(
                "::carven::runtime::checked_sequence_index",
                "carven/runtime/sequence.hpp",
                true
            );
        case TargetSymbol::RuntimeSourceSite:
            return runtime_symbol_info("::carven::runtime::SourceSite", "carven/runtime/trap.hpp");
        case TargetSymbol::RuntimeUTF8Text:
            return runtime_symbol_info(
                "::carven::runtime::utf8_text",
                "carven/runtime/text/text.hpp",
                true
            );
        case TargetSymbol::RuntimeCheckedUnicodeScalar:
            return runtime_symbol_info(
                "::carven::runtime::checked_unicode_scalar",
                "carven/runtime/text/text.hpp",
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
        case TargetSymbol::StdIsFinite:  return symbol_info("::std::isfinite", "cmath");
        case TargetSymbol::StdBitCast:   return symbol_info("::std::bit_cast", "bit");
        case TargetSymbol::StdMove:      return symbol_info("::std::move", "utility");
        case TargetSymbol::StdAsConst:   return symbol_info("::std::as_const", "utility");
        case TargetSymbol::StdNullopt:   return symbol_info("::std::nullopt", "optional");
        case TargetSymbol::StdNullptr:   return symbol_info("nullptr");
        case TargetSymbol::StdInitializerList:
            return symbol_info("::std::initializer_list", "initializer_list");
        case TargetSymbol::StdOptional:   return symbol_info("::std::optional", "optional");
        case TargetSymbol::StdStringView: return symbol_info("::std::string_view", "string_view");
        case TargetSymbol::StdArray:      return symbol_info("::std::array", "array");
        case TargetSymbol::StdAbort:      return symbol_info("::std::abort", "cstdlib", true);
        case TargetSymbol::RuntimeUnreachable:
            return runtime_symbol_info(
                "::carven::runtime::unreachable",
                "carven/runtime/unreachable.hpp",
                true
            );
        case TargetSymbol::StdVariant: return symbol_info("::std::variant", "variant");
        case TargetSymbol::TestingContext:
            return runtime_symbol_info(
                "::carven::runtime::TestContext",
                "carven/runtime/testing.hpp"
            );
        case TargetSymbol::TestingReporter:
            return runtime_symbol_info(
                "::carven::runtime::TestReporter",
                "carven/runtime/testing.hpp"
            );
    }
    std::unreachable();
}
