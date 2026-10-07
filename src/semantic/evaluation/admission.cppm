module carven:semantic.evaluation.admission;

import :semantic.semir.constant_access;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

// This is the executor's structural capability query. Value-dependent failures,
// including bounds, invalid pointers, and execution budgets, remain runtime checks.
template<typename Operation>
constexpr auto unsupported_execution_operation(const Operation& operation) noexcept
    -> std::optional<std::string_view> {
    if constexpr (std::same_as<Operation, SemCpp> || std::same_as<Operation, SemCppCall>) {
        return "native C++ operation has no compile-time provider";
    } else if constexpr (std::same_as<Operation, SemClosure>) {
        return "closure execution is not supported";
    } else if constexpr (std::same_as<Operation, SemArrayAdopt>
                         || std::same_as<Operation, SemEnumConstructor>) {
        return "operation is not supported in execution";
    } else if constexpr (std::same_as<Operation, SemCast>) {
        switch (operation.kind) {
            case CastKind::Identity:
            case CastKind::PointerRead:
            case CastKind::IntegerToInteger:
            case CastKind::IntegerToBool:
            case CastKind::BoolToInteger:
            case CastKind::CharToU32:
            case CastKind::IntegerToFloating:
            case CastKind::FloatingWiden:
            case CastKind::EnumToInteger:     return std::nullopt;
            default:                          return "cast is not supported in execution";
        }
    } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
        const auto* text = std::get_if<TextIntrinsic>(&operation.operation);
        if (!text) {
            return std::nullopt;
        }
        switch (*text) {
            case TextIntrinsic::FromStr:
            case TextIntrinsic::AsStr:
            case TextIntrinsic::Bytes:
            case TextIntrinsic::Len:
            case TextIntrinsic::IsEmpty:
            case TextIntrinsic::Append:
            case TextIntrinsic::Push:
            case TextIntrinsic::Clear:
            case TextIntrinsic::FromU32Unchecked: return std::nullopt;
            case TextIntrinsic::Chars:
            case TextIntrinsic::FromUTF8Unchecked:
                return "text operation is not supported in execution";
        }
    } else if constexpr (std::same_as<Operation, SemDefault>
                         || std::same_as<Operation, SemConstant>
                         || std::same_as<Operation, SemUnreachable>
                         || std::same_as<Operation, SemBinding>
                         || std::same_as<Operation, SemCallable>
                         || std::same_as<Operation, SemRange>
                         || std::same_as<Operation, SemArray>
                         || std::same_as<Operation, SemStruct>
                         || std::same_as<Operation, SemEnumCase>
                         || std::same_as<Operation, SemUnary>
                         || std::same_as<Operation, SemBinary>
                         || std::same_as<Operation, SemShortCircuit>
                         || std::same_as<Operation, SemDereference>
                         || std::same_as<Operation, SemAddressOf>
                         || std::same_as<Operation, SemField>
                         || std::same_as<Operation, SemIndex>
                         || std::same_as<Operation, SemReport>
                         || std::same_as<Operation, SemPrint>
                         || std::same_as<Operation, SemFormat>
                         || std::same_as<Operation, SemCall>
                         || std::same_as<Operation, SemColdCall>
                         || std::same_as<Operation, SemAsyncIntrinsic>
                         || std::same_as<Operation, SemAwait>
                         || std::same_as<Operation, SemBorrowCallable>
                         || std::same_as<Operation, SemTake>
                         || std::same_as<Operation, SemPropagate>
                         || std::same_as<Operation, SemIf>
                         || std::same_as<Operation, SemMatch>
                         || std::same_as<Operation, SemTry>) {
        return std::nullopt;
    } else {
        return "operation is not supported in execution";
    }
    return std::nullopt;
}

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view>;

auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view>;

auto supported_execution_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    bool allow_void = false
) noexcept -> bool;
