module carven:semantic.evaluation.text.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.format.builtin;
import :support.utf8;
import std;

auto SemanticExecutor::text_storage(
    ExecutionFrame& frame,
    const ExecutionPlace& place,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionOwnedText*> {
    auto selected = located(frame, place, origin);
    if (!selected) {
        return std::unexpected(selected.error());
    }
    if (auto* text = std::get_if<ExecutionOwnedText>(*selected)) {
        return text;
    }
    return std::unexpected(
        fail(origin, DiagnosticCode::ConstEvaluation, "text mutation requires String storage")
    );
}

auto SemanticExecutor::append_text(
    ExecutionFrame& frame,
    const ExecutionPlace& destination,
    std::string_view bytes,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    auto target = text_storage(frame, destination, origin);
    if (!target) {
        return std::unexpected(target.error());
    }
    if (bytes.size() > maximum_constant_text_bytes - (*target)->bytes.size()) {
        return std::unexpected(fail(origin, DiagnosticCode::ConstLimit, "text exceeds 1 MiB"));
    }
    if (auto checked = account_text(bytes.size(), origin); !checked) {
        return std::unexpected(checked.error());
    }
    (*target)->bytes += bytes;
    return ExecutionVoid {};
}

auto SemanticExecutor::text_intrinsic(
    ExecutionFrame& frame,
    const SemTextIntrinsic& operation,
    TypeID result_type,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    switch (operation.intrinsic) {
        case TextIntrinsic::New:    return ExecutionOwnedText {.bytes = {}};
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
                return ExecutionVoid {};
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
                    return std::unexpected(
                        fail(origin, DiagnosticCode::ConstEvaluation, "String.push requires a char")
                    );
                }
                append_utf8(suffix, character->scalar);
            } else {
                return std::unexpected(fail(
                    origin,
                    DiagnosticCode::ConstEvaluation,
                    "operation does not implement text mutation"
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
    const auto* receiver = std::get_if<ExecutionValue>(&*operand);
    if (receiver == nullptr) {
        auto selected = located(frame, std::get<ExecutionPlace>(*operand), origin);
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
            return ExecutionOwnedText {.bytes = std::string(*bytes)};
        case TextIntrinsic::AsStr:
            if (auto checked = account_text(bytes->size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
            return ExecutionText {.bytes = std::make_shared<const std::string>(*bytes)};
        case TextIntrinsic::Bytes: {
            if (auto checked = account_aggregate(bytes->size(), origin); !checked) {
                return std::unexpected(checked.error());
            }
            const auto element_type = values.builtin_type(BuiltinType::U8);
            auto elements = std::vector<ExecutionValue>();
            for (const auto byte : *bytes) {
                elements.emplace_back(
                    ConstantAtom {
                        .type = element_type,
                        .value =
                            IntegerConstant::from_parts(static_cast<unsigned char>(byte), false)
                    }
                );
            }
            return ExecutionAggregateValue {
                .type = result_type,
                .elements = std::move(elements),
            };
        }
        case TextIntrinsic::Len:
            return ConstantAtom {
                .type = values.builtin_type(BuiltinType::Usize),
                .value = IntegerConstant::from_parts(bytes->size(), false),
            };
        case TextIntrinsic::IsEmpty:
            return ConstantAtom {
                .type = values.builtin_type(BuiltinType::Bool),
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
                "text operation is not supported in execution"
            ));
    }
    std::unreachable();
}

auto SemanticExecutor::format(
    ExecutionFrame& frame,
    const SemFormat& operation,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    auto destination = std::optional<ExecutionPlace>();
    if (operation.receiver) {
        auto selected = place(frame, **operation.receiver);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        destination = std::move(*selected);
    }
    auto operands = std::vector<ExecutionOperand>();
    for (const auto& operand : operation.operands) {
        auto result = read_operand(frame, operand.expression);
        if (!result) {
            return std::unexpected(result.error());
        }
        operands.push_back(std::move(*result));
    }
    auto arguments = std::vector<ExecutionValue>();
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
        if (const auto text = execution_text(values, argument)) {
            observed.emplace_back(*text);
        } else if (const auto atom = execution_atom(values, argument)) {
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
    return ExecutionOwnedText {.bytes = std::move(*result)};
}

auto SemanticExecutor::print(
    ExecutionFrame& frame,
    const SemPrint& operation,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    auto operands = std::vector<ExecutionOperand>();
    for (const auto& operand : operation.operands) {
        auto result = read_operand(frame, operand.expression);
        if (!result) {
            return std::unexpected(result.error());
        }
        operands.push_back(std::move(*result));
    }
    const auto stream = operation.kind == PrintKind::Eprint || operation.kind == PrintKind::Eprintln
        ? ExecutionOutputStream::Error
        : ExecutionOutputStream::Standard;
    const auto write = [&](std::string_view bytes) noexcept -> ExecutionResult<void> {
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
        if (const auto bytes = execution_text(values, *argument)) {
            observed = *bytes;
        } else if (const auto atom = execution_atom(values, *argument)) {
            observed = builtin_format_value(values, constant_fact(*atom));
        }
        auto formatted = format_builtin_value(observed, {}, maximum_constant_text_bytes);
        if (!formatted) {
            return std::unexpected(fail(
                origin,
                formatted.error() == BuiltinFormatFailureKind::Limit
                    ? DiagnosticCode::ConstLimit
                    : DiagnosticCode::ConstEvaluation,
                "value cannot be printed within the execution limits"
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
    return ExecutionVoid {};
}
