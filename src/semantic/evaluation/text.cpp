module carven:semantic.evaluation.text.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.format.builtin;
import :support.utf8;
import std;

auto ConstantExecutor::text_storage(
    ConstantFrame& frame,
    const ConstantPlace& place,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantOwnedText*> {
    auto selected = located(frame, place, origin);
    if (!selected) {
        return std::unexpected(selected.error());
    }
    if (auto* text = std::get_if<ConstantOwnedText>(*selected)) {
        return text;
    }
    return std::unexpected(
        fail(origin, DiagnosticCode::ConstEvaluation, "text mutation requires String storage")
    );
}

auto ConstantExecutor::append_text(
    ConstantFrame& frame,
    const ConstantPlace& destination,
    std::string_view bytes,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    auto target = text_storage(frame, destination, origin);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (bytes.size() > maximum_constant_text_bytes - (*target)->bytes.size()) {
        return std::unexpected(
            fail(origin, DiagnosticCode::ConstLimit, "constant text exceeds 1 MiB")
        );
    }
    if (auto checked = account_text(bytes.size(), origin); !checked) {
        return std::unexpected(checked.error());
    }
    (*target)->bytes += bytes;
    return ConstantVoid {};
}

auto ConstantExecutor::text_intrinsic(
    ConstantFrame& frame,
    const SemTextIntrinsic& operation,
    TypeID result_type,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    switch (operation.intrinsic) {
        case TextIntrinsic::New:    return ConstantOwnedText {.bytes = {}};
        case TextIntrinsic::Clear:
        case TextIntrinsic::Append:
        case TextIntrinsic::Push:   {
            auto receiver = place(frame, operation.operands[0].expression);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (operation.intrinsic == TextIntrinsic::Clear) {
                auto target = text_storage(frame, *receiver, origin);
                if (!target) {
                    return std::unexpected(target.error());
                }
                (*target)->bytes.clear();
                return ConstantVoid {};
            }
            auto operand = value(frame, operation.operands[1].expression);
            if (!operand) {
                return std::unexpected(operand.error());
            }
            auto suffix = std::string();
            if (operation.intrinsic == TextIntrinsic::Append) {
                auto bytes = text(*operand, origin);
                if (!bytes) {
                    return std::unexpected(bytes.error());
                }
                suffix = *bytes;
            } else if (operation.intrinsic == TextIntrinsic::Push) {
                auto fact = read_fact(*operand, origin);
                if (!fact) {
                    return std::unexpected(fact.error());
                }
                const auto* character = std::get_if<CharacterConstant>(&fact->value);
                if (character == nullptr) {
                    return std::unexpected(fail(
                        origin,
                        DiagnosticCode::ConstEvaluation,
                        "String.push requires a constant char"
                    ));
                }
                append_utf8(suffix, character->scalar);
            } else {
                return std::unexpected(fail(
                    origin,
                    DiagnosticCode::ConstEvaluation,
                    "operation does not implement constant text mutation"
                ));
            }
            return append_text(frame, *receiver, suffix, origin);
        }
        case TextIntrinsic::Len:
        case TextIntrinsic::IsEmpty:
        case TextIntrinsic::Bytes:
        case TextIntrinsic::Chars:
        case TextIntrinsic::FromStr:
        case TextIntrinsic::FromUTF8Unchecked:
        case TextIntrinsic::FromU32Unchecked:
        case TextIntrinsic::AsStr:             break;
    }
    auto operand = read_operand(frame, operation.operands[0].expression);
    if (!operand) {
        return std::unexpected(operand.error());
    }
    auto* receiver = std::get_if<ConstantExecutionValue>(&*operand);
    if (receiver == nullptr) {
        auto selected = located(frame, std::get<ConstantPlace>(*operand), origin);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        receiver = *selected;
    }
    auto bytes = text(*receiver, origin);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    switch (operation.intrinsic) {
        case TextIntrinsic::FromStr:
            if (auto checked = account_text(bytes->size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
            return ConstantOwnedText {.bytes = std::string(*bytes)};
        case TextIntrinsic::AsStr:
            if (auto checked = account_text(bytes->size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
            return ConstantText {.bytes = std::make_shared<const std::string>(*bytes)};
        case TextIntrinsic::Bytes: {
            if (auto checked = account_aggregate(bytes->size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
            const auto element_type = values.intern_builtin_type(BuiltinType::U8);
            auto elements = std::vector<ConstantExecutionValue>();
            for (const auto byte : *bytes) {
                elements.emplace_back(
                    ConstantAtom {
                        .type = element_type,
                        .value =
                            IntegerConstant::from_parts(static_cast<unsigned char>(byte), false)
                    }
                );
            }
            return ConstantAggregateValue {
                .type = result_type,
                .elements = std::move(elements),
            };
        }
        case TextIntrinsic::Len:
            return ConstantAtom {
                .type = values.intern_builtin_type(BuiltinType::Usize),
                .value = IntegerConstant::from_parts(bytes->size(), false),
            };
        case TextIntrinsic::IsEmpty:
            return ConstantAtom {
                .type = values.intern_builtin_type(BuiltinType::Bool),
                .value = BooleanConstant {.value = bytes->empty()},
            };
        case TextIntrinsic::New:
        case TextIntrinsic::Clear:
        case TextIntrinsic::Append:
        case TextIntrinsic::Push:
        case TextIntrinsic::Chars:
        case TextIntrinsic::FromUTF8Unchecked:
        case TextIntrinsic::FromU32Unchecked:
            return std::unexpected(fail(
                origin,
                DiagnosticCode::ConstEvaluation,
                "text operation is not supported in const execution"
            ));
    }
    std::unreachable();
}

auto ConstantExecutor::format(
    ConstantFrame& frame,
    const SemFormat& operation,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    auto destination = std::optional<ConstantPlace>();
    if (operation.receiver) {
        auto selected = place(frame, **operation.receiver);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        destination = std::move(*selected);
    }
    auto operands = std::vector<ConstantOperand>();
    for (const auto& operand : operation.operands) {
        auto result = read_operand(frame, operand.expression);
        if (!result) {
            return std::unexpected(result.error());
        }
        operands.push_back(std::move(*result));
    }
    auto arguments = std::vector<ConstantExecutionValue>();
    for (auto& operand : operands) {
        auto result = materialize(frame, std::move(operand), origin);
        if (!result) {
            return std::unexpected(result.error());
        }
        arguments.push_back(std::move(*result));
    }
    auto observed = std::vector<BuiltinFormatValue>();
    observed.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (const auto text = constant_execution_text(values, argument)) {
            observed.emplace_back(*text);
        } else if (const auto atom = constant_execution_atom(values, argument)) {
            observed.push_back(builtin_format_value(values, constant_fact(*atom)));
        } else {
            observed.emplace_back();
        }
    }
    auto result = format_builtin(operation.specification, observed, maximum_constant_text_bytes);
    if (!result) {
        return std::unexpected(fail(
            origin,
            result.error().kind == BuiltinFormatFailureKind::Limit
                ? DiagnosticCode::ConstLimit
                : DiagnosticCode::ConstEvaluation,
            std::string(result.error().message)
        ));
    }
    if (auto checked = account_text(result->size(), origin); !checked) {
        return std::unexpected(checked.error());
    }
    if (destination) {
        return append_text(frame, *destination, *result, origin);
    }
    return ConstantOwnedText {.bytes = std::move(*result)};
}

auto ConstantExecutor::print(
    ConstantFrame& frame,
    const SemPrint& operation,
    ProgramOriginID origin
) noexcept -> ConstantExecutionResult<ConstantExecutionValue> {
    auto operands = std::vector<ConstantOperand>();
    for (const auto& operand : operation.operands) {
        auto result = read_operand(frame, operand.expression);
        if (!result) {
            return std::unexpected(result.error());
        }
        operands.push_back(std::move(*result));
    }
    const auto stream = operation.kind == PrintKind::Eprint || operation.kind == PrintKind::Eprintln
        ? ConstantOutputStream::Error
        : ConstantOutputStream::Standard;
    const auto write = [&](std::string_view bytes) noexcept -> ConstantExecutionResult<void> {
        if (auto checked = account_text(bytes.size(), origin); !checked) {
            return checked;
        }
        context.write(stream, bytes);
        return {};
    };
    for (auto index = 0uz; index < operands.size(); ++index) {
        auto argument = materialize(frame, std::move(operands[index]), origin);
        if (!argument) {
            return std::unexpected(argument.error());
        }
        auto observed = BuiltinFormatValue();
        if (const auto bytes = constant_execution_text(values, *argument)) {
            observed = *bytes;
        } else if (const auto atom = constant_execution_atom(values, *argument)) {
            observed = builtin_format_value(values, constant_fact(*atom));
        }
        auto formatted = format_builtin_value(observed, {}, maximum_constant_text_bytes);
        if (!formatted) {
            return std::unexpected(fail(
                origin,
                formatted.error() == BuiltinFormatFailureKind::Limit
                    ? DiagnosticCode::ConstLimit
                    : DiagnosticCode::ConstEvaluation,
                "value cannot be printed within the constant execution limits"
            ));
        }
        if (index != 0) {
            if (auto written = write(" "); !written) {
                return std::unexpected(written.error());
            }
        }
        if (auto written = write(*formatted); !written) {
            return std::unexpected(written.error());
        }
    }
    if (operation.kind == PrintKind::Println || operation.kind == PrintKind::Eprintln) {
        if (auto written = write("\n"); !written) {
            return std::unexpected(written.error());
        }
    }
    return ConstantVoid {};
}
