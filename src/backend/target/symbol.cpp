module carven:backend.target.symbol.impl;

import :backend.target.header;
import :backend.target.symbol;
import std;

namespace {

auto symbol_info(
    std::string_view spelling,
    std::string_view header = {},
    TargetHeaderGroup group = TargetHeaderGroup::StandardLibrary
) noexcept -> TargetSymbolInfo {
    return {
        .spelling = spelling,
        .header = header.empty()
            ? std::nullopt
            : std::optional(TargetHeaderProvider {.group = group, .path = header}),
    };
}

auto runtime_symbol_info(std::string_view spelling, std::string_view header) noexcept
    -> TargetSymbolInfo {
    return symbol_info(spelling, header, TargetHeaderGroup::Runtime);
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
        case TargetSymbol::CoAwait:        return symbol_info("co_await", "coroutine");
        case TargetSymbol::CoReturn:       return symbol_info("co_return", "coroutine");
        case TargetSymbol::RuntimeDeferredResult:
            return runtime_symbol_info(
                "::carven::runtime::DeferredResult",
                "carven/runtime/value/deferred.hpp"
            );
        case TargetSymbol::RuntimeVoid:
            return runtime_symbol_info("::carven::runtime::Void", "carven/runtime/value/void.hpp");
        case TargetSymbol::RuntimeNativeValue:
            return runtime_symbol_info(
                "::carven::runtime::NativeValue",
                "carven/runtime/value/void.hpp"
            );
        case TargetSymbol::RuntimeMaterializeNative:
            return runtime_symbol_info(
                "::carven::runtime::materialize_native",
                "carven/runtime/value/void.hpp"
            );
        case TargetSymbol::RuntimeTuple:
            return runtime_symbol_info(
                "::carven::runtime::Tuple",
                "carven/runtime/value/tuple.hpp"
            );
        case TargetSymbol::RuntimeTupleGet:
            return runtime_symbol_info(
                "::carven::runtime::tuple_get",
                "carven/runtime/value/tuple.hpp"
            );
        case TargetSymbol::RuntimeTransfer:
            return runtime_symbol_info(
                "::carven::runtime::transfer",
                "carven/runtime/call/passing.hpp"
            );
        case TargetSymbol::RuntimeReadArg:
            return runtime_symbol_info(
                "::carven::runtime::ReadArg",
                "carven/runtime/call/passing.hpp"
            );
        case TargetSymbol::RuntimeReadArgumentStorage:
            return runtime_symbol_info(
                "::carven::runtime::ReadArgumentStorage",
                "carven/runtime/call/passing.hpp"
            );
        case TargetSymbol::RuntimeReadConversionStorage:
            return runtime_symbol_info(
                "::carven::runtime::ReadConversionStorage",
                "carven/runtime/call/passing.hpp"
            );
        case TargetSymbol::RuntimeReadConversion:
            return runtime_symbol_info(
                "::carven::runtime::read_conversion",
                "carven/runtime/call/passing.hpp"
            );
        case TargetSymbol::RuntimeNativeTestResult:
            return runtime_symbol_info(
                "::carven::runtime::native_test_result",
                "carven/runtime/testing/testing.hpp"
            );
        case TargetSymbol::RuntimeTestStopped:
            return runtime_symbol_info(
                "::carven::runtime::TestStopped",
                "carven/runtime/testing/testing.hpp"
            );
        case TargetSymbol::RuntimeAssertionFailed:
            return runtime_symbol_info(
                "::carven::runtime::assertion_failed",
                "carven/runtime/diagnostics/report.hpp"
            );
        case TargetSymbol::RuntimeReportTestFailure:
            return runtime_symbol_info(
                "::carven::runtime::report_test_failure",
                "carven/runtime/testing/testing.hpp"
            );
        case TargetSymbol::RuntimeAsyncOperation:
            return runtime_symbol_info(
                "::carven::runtime::execution::AsyncOperation",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncCompletion:
            return runtime_symbol_info(
                "::carven::runtime::execution::Completion",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncSuccessBinding:
            return runtime_symbol_info(
                "::carven::runtime::execution::SuccessBinding",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncChild:
            return runtime_symbol_info(
                "::carven::runtime::execution::Child",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncChildScope:
            return runtime_symbol_info(
                "::carven::runtime::execution::ChildScope",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncCurrentActivation:
            return runtime_symbol_info(
                "::carven::runtime::execution::current_activation",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncClosingPolicy:
            return runtime_symbol_info(
                "::carven::runtime::execution::ClosingPolicy",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncCancel:
            return runtime_symbol_info(
                "::carven::runtime::execution::cancel",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncWaitReady:
            return runtime_symbol_info(
                "::carven::runtime::execution::wait_ready",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncClose:
            return runtime_symbol_info(
                "::carven::runtime::execution::close",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncWhenAll:
            return runtime_symbol_info(
                "::carven::runtime::execution::when_all",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncWhenAny:
            return runtime_symbol_info(
                "::carven::runtime::execution::when_any",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncFirstSuccessful:
            return runtime_symbol_info(
                "::carven::runtime::execution::first_successful",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncTimeout:
            return runtime_symbol_info(
                "::carven::runtime::execution::timeout",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncCompositionOperand:
            return runtime_symbol_info(
                "::carven::runtime::execution::composition_operand",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncCancellationRequested:
            return runtime_symbol_info(
                "::carven::runtime::execution::cancellation_requested",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncCancelPoint:
            return runtime_symbol_info(
                "::carven::runtime::execution::cancel_point",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncYieldOnce:
            return runtime_symbol_info(
                "::carven::runtime::execution::yield_once",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncAwaitCancelPoint:
            return runtime_symbol_info(
                "::carven::runtime::execution::await_cancel_point",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncAwaitYieldOnce:
            return runtime_symbol_info(
                "::carven::runtime::execution::await_yield_once",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncDriveRoot:
            return runtime_symbol_info(
                "::carven::runtime::execution::drive_root",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeAsyncReportEntryCancelled:
            return runtime_symbol_info(
                "::carven::runtime::execution::report_entry_cancelled",
                "carven/runtime/execution/execution.hpp"
            );
        case TargetSymbol::RuntimeOutcome:
            return runtime_symbol_info(
                "::carven::runtime::Outcome",
                "carven/runtime/value/outcome.hpp"
            );
        case TargetSymbol::RuntimeObserveComparison:
            return runtime_symbol_info(
                "::carven::runtime::observe_comparison",
                "carven/runtime/diagnostics/report.hpp"
            );
        case TargetSymbol::RuntimeObserveShortCircuit:
            return runtime_symbol_info(
                "::carven::runtime::observe_short_circuit",
                "carven/runtime/diagnostics/report.hpp"
            );
        case TargetSymbol::RuntimeDisplayWriter:
            return runtime_symbol_info(
                "::carven::runtime::DisplayWriter",
                "carven/runtime/display/display.hpp"
            );
        case TargetSymbol::RuntimeStatelessValue:
            return runtime_symbol_info(
                "::carven::runtime::stateless_value",
                "carven/runtime/value/stateless.hpp"
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
            return runtime_symbol_info("::carven::runtime::print", "carven/runtime/io/print.hpp");
        case TargetSymbol::RuntimePrintln:
            return runtime_symbol_info("::carven::runtime::println", "carven/runtime/io/print.hpp");
        case TargetSymbol::RuntimeEprint:
            return runtime_symbol_info("::carven::runtime::eprint", "carven/runtime/io/print.hpp");
        case TargetSymbol::RuntimeEprintln:
            return runtime_symbol_info(
                "::carven::runtime::eprintln",
                "carven/runtime/io/print.hpp"
            );
        case TargetSymbol::RuntimeFormat:
            return runtime_symbol_info(
                "::carven::runtime::format",
                "carven/runtime/format/format.hpp"
            );
        case TargetSymbol::RuntimeFormatValidUTF8:
            return runtime_symbol_info(
                "::carven::runtime::format_valid_utf8",
                "carven/runtime/format/format.hpp"
            );
        case TargetSymbol::RuntimeAppendFormat:
            return runtime_symbol_info(
                "::carven::runtime::append_format",
                "carven/runtime/format/format.hpp"
            );
        case TargetSymbol::RuntimeAppendFormatValidUTF8:
            return runtime_symbol_info(
                "::carven::runtime::append_format_valid_utf8",
                "carven/runtime/format/format.hpp"
            );
        case TargetSymbol::RuntimeAdoptArray:
            return runtime_symbol_info(
                "::carven::runtime::adopt_array",
                "carven/runtime/collection/array.hpp"
            );
        case TargetSymbol::RuntimeAsSlice:
            return runtime_symbol_info(
                "::carven::runtime::as_slice",
                "carven/runtime/collection/slice.hpp"
            );
        case TargetSymbol::RuntimeRange:
            return runtime_symbol_info(
                "::carven::runtime::Range",
                "carven/runtime/collection/range.hpp"
            );
        case TargetSymbol::RuntimeSlice:
            return runtime_symbol_info(
                "::carven::runtime::Slice",
                "carven/runtime/collection/slice.hpp"
            );
        case TargetSymbol::RuntimeVector:
            return runtime_symbol_info(
                "::carven::runtime::Vector",
                "carven/runtime/collection/vector.hpp"
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
            return runtime_symbol_info(
                "::carven::runtime::EntryArgs",
                "carven/runtime/execution/entry.hpp"
            );
        case TargetSymbol::RuntimeEntryArgs:
            return runtime_symbol_info(
                "::carven::runtime::entry_args",
                "carven/runtime/execution/entry.hpp"
            );
        case TargetSymbol::RuntimeReportEntryFailure:
            return runtime_symbol_info(
                "::carven::runtime::report_entry_failure",
                "carven/runtime/execution/entry.hpp"
            );
        case TargetSymbol::RuntimeFunctionRef:
            return runtime_symbol_info(
                "::carven::runtime::FunctionRef",
                "carven/runtime/call/callable.hpp"
            );
        case TargetSymbol::RuntimeTextBytes:
            return runtime_symbol_info(
                "::carven::runtime::text_bytes",
                "carven/runtime/text/text.hpp"
            );
        case TargetSymbol::RuntimeTextChars:
            return runtime_symbol_info(
                "::carven::runtime::text_chars",
                "carven/runtime/text/text.hpp"
            );
        case TargetSymbol::RuntimeIntegerNegate:
            return runtime_symbol_info(
                "::carven::runtime::integer_negate",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeIntegerAdd:
            return runtime_symbol_info(
                "::carven::runtime::integer_add",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeIntegerSubtract:
            return runtime_symbol_info(
                "::carven::runtime::integer_subtract",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeIntegerMultiply:
            return runtime_symbol_info(
                "::carven::runtime::integer_multiply",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeIntegerDivide:
            return runtime_symbol_info(
                "::carven::runtime::integer_divide",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeFormatWriter:
            return runtime_symbol_info(
                "::carven::runtime::FormatWriter",
                "carven/runtime/format/writer.hpp"
            );
        case TargetSymbol::RuntimeIntegerRemainder:
            return runtime_symbol_info(
                "::carven::runtime::integer_remainder",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeIntegerLeftShift:
            return runtime_symbol_info(
                "::carven::runtime::integer_left_shift",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeIntegerRightShift:
            return runtime_symbol_info(
                "::carven::runtime::integer_right_shift",
                "carven/runtime/numeric/numeric.hpp"
            );
        case TargetSymbol::RuntimeCheckedArrayIndex:
            return runtime_symbol_info(
                "::carven::runtime::checked_array_index",
                "carven/runtime/collection/array.hpp"
            );
        case TargetSymbol::RuntimeCheckedSliceIndex:
            return runtime_symbol_info(
                "::carven::runtime::checked_slice_index",
                "carven/runtime/collection/slice.hpp"
            );
        case TargetSymbol::RuntimeCheckedVectorIndex:
            return runtime_symbol_info(
                "::carven::runtime::checked_vector_index",
                "carven/runtime/collection/vector.hpp"
            );
        case TargetSymbol::RuntimeSourceSite:
            return runtime_symbol_info(
                "::carven::runtime::SourceSite",
                "carven/runtime/diagnostics/trap.hpp"
            );
        case TargetSymbol::RuntimeUTF8Text:
            return runtime_symbol_info(
                "::carven::runtime::utf8_text",
                "carven/runtime/text/text.hpp"
            );
        case TargetSymbol::RuntimeCheckedUnicodeScalar:
            return runtime_symbol_info(
                "::carven::runtime::checked_unicode_scalar",
                "carven/runtime/text/text.hpp"
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
        case TargetSymbol::StdAbort:      return symbol_info("::std::abort", "cstdlib");
        case TargetSymbol::RuntimeUnreachable:
            return runtime_symbol_info(
                "::carven::runtime::unreachable",
                "carven/runtime/execution/unreachable.hpp"
            );
        case TargetSymbol::StdVariant: return symbol_info("::std::variant", "variant");
        case TargetSymbol::TestingContext:
            return runtime_symbol_info(
                "::carven::runtime::TestContext",
                "carven/runtime/testing/testing.hpp"
            );
        case TargetSymbol::TestingReporter:
            return runtime_symbol_info(
                "::carven::runtime::TestReporter",
                "carven/runtime/testing/testing.hpp"
            );
    }
    std::unreachable();
}
