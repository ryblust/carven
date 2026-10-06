module carven:backend.target.symbol;

import :backend.target.header;
import std;

enum class TargetSymbol {
    Auto,
    DecltypeAuto,
    Void,
    Bool,
    Char,
    Int,
    CChar,
    StdInt8,
    StdInt16,
    StdInt32,
    StdInt64,
    StdUInt8,
    StdUInt16,
    StdUInt32,
    StdUInt64,
    StdPtrdiff,
    StdSize,
    Float,
    Double,
    RuntimeReadArg,
    RuntimeTransfer,
    RuntimeDeferredResult,
    RuntimeNativeTestResult,
    RuntimeTestStopped,
    RuntimeReportTestFailure,
    RuntimeAssertionFailed,
    RuntimeOutcome,
    RuntimeString,
    RuntimeU8x16,
    RuntimeU8x32,
    RuntimeMask16,
    RuntimeMask32,
    RuntimeF32x4,
    RuntimeF32x8,
    RuntimeMask4,
    RuntimeMask8,
    RuntimeSlice,
    RuntimeRange,
    RuntimeAsSlice,
    RuntimeAdoptArray,
    RuntimeObserveComparison,
    RuntimeObserveShortCircuit,
    RuntimeDisplayWriter,
    RuntimeStatelessValue,
    RuntimeStructuralDisplay,
    RuntimeScalarDisplay,
    RuntimeSequenceDisplay,
    RuntimeRangeDisplay,
    RuntimePrint,
    RuntimePrintln,
    RuntimeEprint,
    RuntimeEprintln,
    RuntimeFormat,
    RuntimeFormatValidUTF8,
    RuntimeAppendFormat,
    RuntimeAppendFormatValidUTF8,
    RuntimeWriter,
    RuntimeStrCharsView,
    RuntimeEntryArgs,
    RuntimeEntryArgsType,
    RuntimeReportEntryFailure,
    RuntimeFunctionRef,
    RuntimeTextBytes,
    RuntimeTextChars,
    RuntimeIntegerNegate,
    RuntimeIntegerAdd,
    RuntimeIntegerSubtract,
    RuntimeIntegerMultiply,
    RuntimeIntegerDivide,
    RuntimeIntegerRemainder,
    RuntimeIntegerLeftShift,
    RuntimeIntegerRightShift,
    RuntimeCheckedArrayIndex,
    RuntimeCheckedSliceIndex,
    RuntimeSourceSite,
    RuntimeCheckedUnicodeScalar,
    RuntimeUTF8Text,
    StdAddConst,
    StdRemoveCVRef,
    StdTypeIdentity,
    StdReferenceWrapper,
    StdAddressof,
    StdToArray,
    StdBitCast,
    StdGetIf,
    StdDeclval,
    StdForward,
    StdMove,
    StdAsConst,
    StdNullopt,
    StdNullptr,
    StdExitFailure,
    StdOptional,
    StdInitializerList,
    StdStringView,
    StdVariant,
    StdArray,
    StdAbort,
    RuntimeUnreachable,
    TestingContext,
    TestingReporter,
};

struct TargetSymbolInfo final {
    std::string_view spelling;
    std::optional<TargetHeaderProvider> header;
    // The call-result contract does not permit omitting execution.
    bool allows_implicit_discard;
};

auto target_symbol_info(TargetSymbol symbol) noexcept -> TargetSymbolInfo;

// Nodes declare their native facility; rendering and dependency discovery share it.
template<typename Node>
auto target_node_symbol(const Node& node) noexcept -> std::optional<TargetSymbol> {
    if constexpr (requires { node.native_symbol(); }) {
        return node.native_symbol();
    } else {
        return std::nullopt;
    }
}
