module carven:semantic.evaluation.text.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.format.builtin;
import :support.utf8;
import std;

auto SemanticExecutor::text_handle(const ExecutionValue& value, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionText> {
    if (const auto* text = std::get_if<ExecutionOwnedText>(&value)) {
        return text->borrow();
    }
    if (const auto* text = std::get_if<ExecutionText>(&value)) {
        if (!text->bytes()) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
            );
        }
        return *text;
    }
    const auto atom = execution_atom(values, value);
    const auto* text = atom ? std::get_if<StringConstant>(&atom->value) : nullptr;
    if (!text) {
        return std::unexpected(fail(origin, ExecutionReason::Evaluation, "expected text"));
    }
    if (const auto found = retained_text.find(text->value); found != retained_text.end()) {
        return found->second;
    }
    // Spellings remain stable while this execution borrows its value reader.
    auto handle = ExecutionText::retained(values.spelling(text->value));
    retained_text.emplace(text->value, handle);
    return handle;
}

auto SemanticExecutor::text_storage(const ExecutionPlace& place, ProgramOriginID origin) noexcept
    -> ExecutionResult<ExecutionOwnedText*> {
    auto selected = located(place, origin);
    if (!selected) {
        return std::unexpected(std::move(selected.error()));
    }
    if (auto* text = std::get_if<ExecutionOwnedText>(*selected)) {
        return text;
    }
    return std::unexpected(
        fail(origin, ExecutionReason::Evaluation, "text mutation requires String storage")
    );
}

auto SemanticExecutor::append_text(
    const ExecutionPlace& destination,
    std::string_view bytes,
    ProgramOriginID origin
) noexcept -> ExecutionResult<ExecutionValue> {
    auto target = text_storage(destination, origin);
    if (!target) {
        return std::unexpected(std::move(target.error()));
    }
    if (bytes.size() > maximum_constant_text_bytes - (*target)->bytes().size()) {
        return std::unexpected(fail(origin, ExecutionReason::Limit, "text exceeds 1 MiB"));
    }
    if (auto checked = account_text(bytes.size(), origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    (*target)->append(bytes);
    return ExecutionVoid {};
}

auto SemanticExecutor::text_intrinsic(
    ExecutionFrame& frame,
    const SemIntrinsic& operation,
    TypeID result_type,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto intrinsic = std::get<TextIntrinsic>(operation.operation);
    switch (intrinsic) {
        case TextIntrinsic::FromU32Unchecked: {
            auto operand = (co_await this->value(frame, operation.operands[0].expression));
            if (!operand) {
                co_return std::unexpected(std::move(operand.error()));
            }
            auto fact = read_fact(*operand, origin);
            if (!fact) {
                co_return std::unexpected(std::move(fact.error()));
            }
            const auto* integer = std::get_if<IntegerConstant>(&fact->value);
            if (integer == nullptr) {
                co_return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "character construction requires an integer operand"
                ));
            }
            if (integer->negative()
                || integer->magnitude() > 0x10ffffu
                || (integer->magnitude() >= 0xd800u && integer->magnitude() <= 0xdfffu)) {
                co_return std::unexpected(trap(
                    origin,
                    ExecutionReason::Evaluation,
                    "char::from_u32_unchecked requires a Unicode scalar value"
                ));
            }
            co_return ConstantAtom {
                .type = result_type,
                .value = CharacterConstant {.scalar = static_cast<char32_t>(integer->magnitude())},
            };
        }
        case TextIntrinsic::Clear:
        case TextIntrinsic::Append:
        case TextIntrinsic::Push:   {
            auto receiver = (co_await place(frame, operation.operands[0].expression));
            if (!receiver) {
                co_return std::unexpected(std::move(receiver.error()));
            }
            if (intrinsic == TextIntrinsic::Clear) {
                auto target = text_storage(*receiver, origin);
                if (!target) {
                    co_return std::unexpected(std::move(target.error()));
                }
                **target = ExecutionOwnedText(std::string());
                co_return ExecutionVoid {};
            }
            auto operand = (co_await this->value(frame, operation.operands[1].expression));
            if (!operand) {
                co_return std::unexpected(std::move(operand.error()));
            }
            auto suffix = std::string();
            if (intrinsic == TextIntrinsic::Append) {
                auto bytes = text(*operand, origin);
                if (!bytes) {
                    co_return std::unexpected(std::move(bytes.error()));
                }
                suffix = *bytes;
            } else if (intrinsic == TextIntrinsic::Push) {
                auto fact = read_fact(*operand, origin);
                if (!fact) {
                    co_return std::unexpected(std::move(fact.error()));
                }
                const auto* character = std::get_if<CharacterConstant>(&fact->value);
                if (character == nullptr) {
                    co_return std::unexpected(
                        fail(origin, ExecutionReason::Evaluation, "String.push requires a char")
                    );
                }
                append_utf8(suffix, character->scalar);
            } else {
                co_return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "operation does not implement text mutation"
                ));
            }
            co_return append_text(*receiver, suffix, origin);
        }
        case TextIntrinsic::Len:
        case TextIntrinsic::IsEmpty:
        case TextIntrinsic::Bytes:
        case TextIntrinsic::Chars:
        case TextIntrinsic::FromStr:
        case TextIntrinsic::FromUTF8Unchecked:
        case TextIntrinsic::AsStr:             break;
    }
    auto operand = (co_await read_operand(frame, operation.operands[0].expression));
    if (!operand) {
        co_return std::unexpected(std::move(operand.error()));
    }
    if ((intrinsic == TextIntrinsic::AsStr || intrinsic == TextIntrinsic::Bytes)
        && std::holds_alternative<ExecutionValue>(*operand)) {
        auto& temporary = std::get<ExecutionValue>(*operand);
        const auto* text = std::get_if<ExecutionText>(&temporary);
        if (std::holds_alternative<ExecutionOwnedText>(temporary)
            || (text && !text->is_borrowed())) {
            auto place = memory.create(std::move(temporary));
            frame.temporaries.push_back(place);
            *operand = std::move(place);
        }
    }
    const auto* receiver = std::get_if<ExecutionValue>(&*operand);
    if (receiver == nullptr) {
        auto selected = located(std::get<ExecutionPlace>(*operand), origin);
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        receiver = *selected;
    }
    auto bytes = text(*receiver, origin);
    if (!bytes) {
        co_return std::unexpected(std::move(bytes.error()));
    }
    switch (intrinsic) {
        case TextIntrinsic::FromStr:
            if (auto checked = account_text(bytes->size(), origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            co_return ExecutionOwnedText(std::string(*bytes));
        case TextIntrinsic::AsStr:
        case TextIntrinsic::Bytes: {
            auto handle = text_handle(*receiver, origin);
            if (!handle) {
                co_return std::unexpected(std::move(handle.error()));
            }
            if (intrinsic == TextIntrinsic::AsStr) {
                co_return handle->borrow();
            }
            auto backing = memory.text_bytes(*handle, values.builtin_type(BuiltinType::U8));
            if (!backing) {
                co_return std::unexpected(
                    fail(origin, ExecutionReason::Evaluation, "text backing is no longer alive")
                );
            }
            co_return ExecutionSlice {
                .type = result_type,
                .backing = std::move(*backing),
                .offset = 0uz,
                .extent = bytes->size()
            };
        }
        case TextIntrinsic::Len:
            co_return ConstantAtom {
                .type = result_type,
                .value = IntegerConstant::from_parts(bytes->size(), false),
            };
        case TextIntrinsic::IsEmpty:
            co_return ConstantAtom {
                .type = result_type,
                .value = BooleanConstant {.value = bytes->empty()},
            };
        case TextIntrinsic::FromU32Unchecked: std::unreachable();
        case TextIntrinsic::Clear:
        case TextIntrinsic::Append:
        case TextIntrinsic::Push:
        case TextIntrinsic::Chars:
        case TextIntrinsic::FromUTF8Unchecked:
            co_return std::unexpected(fail(
                origin,
                ExecutionReason::Evaluation,
                "text operation is not supported in execution"
            ));
    }
    std::unreachable();
}

auto SemanticExecutor::format(
    ExecutionFrame& frame,
    const SemFormat& operation,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    auto destination = std::optional<ExecutionPlace>();
    if (operation.receiver) {
        auto selected = (co_await place(frame, **operation.receiver));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        destination = std::move(*selected);
    }
    auto operands = std::vector<ExecutionOperand>();
    for (const auto& operand : operation.operands) {
        auto result = (co_await read_operand(frame, operand.expression));
        if (!result) {
            co_return std::unexpected(std::move(result.error()));
        }
        operands.push_back(std::move(*result));
    }
    auto arguments = std::vector<ExecutionValue>();
    for (auto& operand : operands) {
        auto result = materialize(std::move(operand), origin);
        if (!result) {
            co_return std::unexpected(std::move(result.error()));
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
        co_return std::unexpected(fail(
            origin,
            result.error().kind == BuiltinFormatFailureKind::Limit ? ExecutionReason::Limit
                                                                   : ExecutionReason::Evaluation,
            std::string(result.error().message)
        ));
    }
    if (auto checked = account_text(result->size(), origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    if (destination) {
        co_return append_text(*destination, *result, origin);
    }
    co_return ExecutionOwnedText(std::move(*result));
}

auto SemanticExecutor::print(
    ExecutionFrame& frame,
    const SemPrint& operation,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    auto operands = std::vector<ExecutionOperand>();
    for (const auto& operand : operation.operands) {
        auto result = (co_await read_operand(frame, operand.expression));
        if (!result) {
            co_return std::unexpected(std::move(result.error()));
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
        const auto& operand = operands[index];
        const auto* argument = std::get_if<ExecutionValue>(&operand);
        if (const auto* place = std::get_if<ExecutionPlace>(&operand)) {
            auto selected = located(*place, origin);
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            argument = *selected;
        }
        auto formatted = display_execution_value(values, *argument, false, &memory);
        if (!formatted) {
            co_return std::unexpected(fail(
                origin,
                ExecutionReason::Evaluation,
                "value cannot be structurally displayed during execution"
            ));
        }
        if (index != 0) {
            if (auto written = (write(" ")); !written) {
                co_return std::unexpected(std::move(written.error()));
            }
        }
        if (auto written = (write(*formatted)); !written) {
            co_return std::unexpected(std::move(written.error()));
        }
    }
    if (operation.kind == PrintKind::Println || operation.kind == PrintKind::Eprintln) {
        if (auto written = (write("\n")); !written) {
            co_return std::unexpected(std::move(written.error()));
        }
    }
    co_return ExecutionVoid {};
}
