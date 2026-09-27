module carven:semantic.evaluation.admission.impl;

import :semantic.evaluation.admission;
import std;

auto supported_execution_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    bool allow_void
) noexcept -> bool {
    const auto* id = std::get_if<TypeID>(&type);
    if (!id) {
        return false;
    }
    auto pending = std::vector<TypeID> {*id};
    auto visited = std::set<TypeID>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto canonical = values.type_copy(current);
        if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            pending.push_back(array->element);
        } else if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
            const auto fields = values.struct_field_types(structure->structure);
            if (!fields) {
                return false;
            }
            pending.insert(pending.end(), fields->begin(), fields->end());
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
            const auto cases = values.enum_case_types(enumeration->enumeration);
            if (!cases) {
                return false;
            }
            for (const auto& item : *cases) {
                pending.insert(pending.end(), item.payload_types.begin(), item.payload_types.end());
            }
        } else if (std::holds_alternative<PointerTypeValue>(canonical.value)
                   || std::holds_alternative<SliceTypeValue>(canonical.value)
                   || std::holds_alternative<FunctionTypeValue>(canonical.value)
                   || std::holds_alternative<CallableViewTypeValue>(canonical.value)
                   || std::holds_alternative<RangeTypeValue>(canonical.value)) {
            // Views do not contain their targets' storage.
        } else if (const auto* cpp = std::get_if<CppTypeValue>(&canonical.value)) {
            if (!std::holds_alternative<CppConstCharPointerType>(cpp->form)) {
                return false;
            }
        } else if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
            if (!builtin_is_numeric(builtin->kind)
                && builtin->kind != BuiltinType::Bool
                && builtin->kind != BuiltinType::Char
                && builtin->kind != BuiltinType::Str
                && builtin->kind != BuiltinType::String
                && !(allow_void && current == *id && builtin->kind == BuiltinType::Void)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view> {
    return source.value.visit(
        [](const auto& value) static noexcept -> std::optional<std::string_view> {
            using Operation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Operation, SemCpp> || std::same_as<Operation, SemCppCall>) {
                return "native C++ operation has no compile-time provider";
            } else if constexpr (std::same_as<Operation, SemClosure>) {
                return "closure execution is not supported";
            } else if constexpr (std::same_as<Operation, SemArrayAdopt>
                                 || std::same_as<Operation, SemEnumConstructor>) {
                return "operation is not supported in execution";
            } else if constexpr (std::same_as<Operation, SemCast>) {
                switch (value.kind) {
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
            } else if constexpr (std::same_as<Operation, SemTextIntrinsic>) {
                switch (value.intrinsic) {
                    case TextIntrinsic::FromStr:
                    case TextIntrinsic::AsStr:
                    case TextIntrinsic::Bytes:
                    case TextIntrinsic::Len:
                    case TextIntrinsic::IsEmpty:
                    case TextIntrinsic::Append:
                    case TextIntrinsic::Push:
                    case TextIntrinsic::Clear:             return std::nullopt;
                    case TextIntrinsic::Chars:
                    case TextIntrinsic::FromUTF8Unchecked:
                    case TextIntrinsic::FromU32Unchecked:
                        return "text operation is not supported in execution";
                }
            } else if constexpr (std::same_as<Operation, SemDefault>
                                 || std::same_as<Operation, SemConstant>
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
                                 || std::same_as<Operation, SemSliceIntrinsic>
                                 || std::same_as<Operation, SemCall>
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
    );
}

auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view> {
    return source.value.visit(
        [](const auto& value) static noexcept -> std::optional<std::string_view> {
            using Operation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Operation, SemReturn>
                          || std::same_as<Operation, SemThrow>
                          || std::same_as<Operation, SemRethrow>
                          || std::same_as<Operation, SemBreak>
                          || std::same_as<Operation, SemContinue>
                          || std::same_as<Operation, SemExpressionStatement>
                          || std::same_as<Operation, SemInitialize>
                          || std::same_as<Operation, SemAssign>
                          || std::same_as<Operation, SemLoop>
                          || std::same_as<Operation, SemRangeLoop>
                          || std::same_as<Operation, OwnedSemanticRegion>) {
                return std::nullopt;
            } else {
                return "control operation is not supported in execution";
            }
        }
    );
}
