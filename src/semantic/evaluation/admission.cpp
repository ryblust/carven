module carven:semantic.evaluation.admission.impl;

import :semantic.evaluation.admission;
import std;

auto supported_execution_type(
    const ExecutionValueAccess& values,
    const ExecutionTypeShapes& shapes,
    ConstructionTypeRef type,
    bool allow_void
) noexcept -> bool {
    const auto* id = std::get_if<TypeID>(&type);
    if (id == nullptr) {
        return false;
    }
    const auto canonical = values.type_copy(*id);

    if (std::holds_alternative<RangeTypeValue>(canonical.value)) {
        return true;
    }
    if (std::holds_alternative<ArrayTypeValue>(canonical.value)
        || std::holds_alternative<StructTypeValue>(canonical.value)) {
        const auto shape = shapes.get(*id);
        return shape && shape->supported;
    }
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    return builtin != nullptr
        && (builtin_is_integer(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char
            || builtin->kind == BuiltinType::Str
            || builtin->kind == BuiltinType::String
            || (allow_void && builtin->kind == BuiltinType::Void));
}

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view> {
    return std::visit(
        [](const auto& value) static noexcept -> std::optional<std::string_view> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, SemCallable>
                          || std::same_as<Value, SemConstant>
                          || std::same_as<Value, SemBinding>
                          || std::same_as<Value, SemStruct>
                          || std::same_as<Value, SemField>
                          || std::same_as<Value, SemRange>
                          || std::same_as<Value, SemArray>
                          || std::same_as<Value, SemIndex>
                          || std::same_as<Value, SemUnary>
                          || std::same_as<Value, SemBinary>
                          || std::same_as<Value, SemShortCircuit>
                          || std::same_as<Value, SemFormat>
                          || std::same_as<Value, SemPrint>
                          || std::same_as<Value, SemTestReport>
                          || std::same_as<Value, SemTake>
                          || std::same_as<Value, SemIf>
                          || std::same_as<Value, SemMatch>) {
                return std::nullopt;
            } else if constexpr (std::same_as<Value, SemCast>) {
                if (value.kind != CastKind::Identity
                    && value.kind != CastKind::IntegerToInteger
                    && value.kind != CastKind::IntegerToBool
                    && value.kind != CastKind::BoolToInteger
                    && value.kind != CastKind::CharToU32) {
                    return "cast is not supported in execution";
                }
            } else if constexpr (std::same_as<Value, SemCall>) {
                return std::nullopt;
            } else if constexpr (std::same_as<Value, SemTextIntrinsic>) {
                switch (value.intrinsic) {
                    case TextIntrinsic::New:
                    case TextIntrinsic::FromStr:
                    case TextIntrinsic::AsStr:
                    case TextIntrinsic::Len:
                    case TextIntrinsic::IsEmpty:
                    case TextIntrinsic::Append:
                    case TextIntrinsic::Push:
                    case TextIntrinsic::Clear:             return std::nullopt;
                    case TextIntrinsic::Bytes:
                    case TextIntrinsic::Chars:
                    case TextIntrinsic::FromUTF8Unchecked:
                    case TextIntrinsic::FromU32Unchecked:
                        return "text operation is not supported in execution";
                }
            } else {
                return "operation is not supported in execution";
            }
            return std::nullopt;
        },
        source.value
    );
}

auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view> {
    return std::visit(
        [](const auto& value) static noexcept -> std::optional<std::string_view> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, SemReturn>
                          || std::same_as<Value, SemBreak>
                          || std::same_as<Value, SemContinue>
                          || std::same_as<Value, SemExpressionStatement>
                          || std::same_as<Value, SemInitialize>
                          || std::same_as<Value, SemLoop>
                          || std::same_as<Value, SemRangeLoop>
                          || std::same_as<Value, OwnedSemanticRegion>) {
                return std::nullopt;
            } else if constexpr (std::same_as<Value, SemAssign>) {
                auto* target = &value.target;
                while (true) {
                    if (const auto* index = std::get_if<SemIndex>(&target->value)) {
                        target = &*index->source;
                    } else if (const auto* field = std::get_if<SemField>(&target->value)) {
                        target = &*field->source;
                    } else {
                        break;
                    }
                }
                if (!std::holds_alternative<SemBinding>(target->value)) {
                    return "execution assignment requires local storage";
                }
            } else {
                return "control operation is not supported in execution";
            }
            return std::nullopt;
        },
        source.value
    );
}
