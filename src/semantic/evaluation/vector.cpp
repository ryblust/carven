module carven:semantic.evaluation.vector.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.evaluation.value;
import :semantic.semir.vector;
import std;

auto SemanticExecutor::append_bytes(
    ExecutionBytes& target,
    const ExecutionSequenceView& source,
    ProgramOriginID origin
) noexcept -> ExecutionResult<void> {
    const auto count = source.visit([](const auto& view) static noexcept { return view.size(); });
    if (count > maximum_constant_bytes || target.bytes.size() > maximum_constant_bytes - count) {
        return std::unexpected(
            fail(origin, ExecutionReason::Limit, "byte sequence exceeds its size limit")
        );
    }
    if (auto checked = account_bytes(count, origin); !checked) {
        return checked;
    }
    if (const auto* bytes = std::get_if<ExecutionByteView>(&source)) {
        target.bytes.append(
            carven::runtime::Slice<std::uint8_t>(std::span(
                reinterpret_cast<const std::uint8_t*>(bytes->bytes.data()),
                bytes->bytes.size()
            ))
        );
        return {};
    }
    const auto length = target.bytes.size() + count;
    if (length > target.bytes.capacity()) {
        target.bytes.reserve(std::max(length, target.bytes.capacity() * 2uz));
    }
    for (const auto& element : std::get<std::span<const ExecutionValue>>(source)) {
        const auto atom = execution_atom(values, element);
        const auto* integer = atom ? std::get_if<IntegerConstant>(&atom->value) : nullptr;
        if (!integer
            || atom->type != target.element
            || integer->negative()
            || integer->magnitude() > 255u) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "byte sequence requires u8 elements")
            );
        }
        target.bytes.push(static_cast<std::uint8_t>(integer->magnitude()));
    }
    return {};
}

auto SemanticExecutor::vector_intrinsic(
    ExecutionFrame& frame,
    const SemIntrinsic& operation,
    TypeID result_type,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto intrinsic = std::get<VectorIntrinsicOperation>(operation.operation).intrinsic;
    if (intrinsic == VectorIntrinsic::FromSlice || intrinsic == VectorIntrinsic::Append) {
        auto receiver = std::optional<ExecutionPlace>();
        if (intrinsic == VectorIntrinsic::Append) {
            auto selected = co_await operand(
                frame,
                operation.operands.front().expression,
                argument_use(AccessMode::Write),
                origin
            );
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            receiver = std::get<ExecutionPlace>(*selected);
        }
        auto view = co_await sequence_view(frame, operation.operands.back().expression, origin);
        if (!view) {
            co_return std::unexpected(std::move(view.error()));
        }
        auto mutation = std::optional<ExecutionPlace>();
        if (receiver) {
            auto storage = mutation_place(*receiver, origin);
            if (!storage) {
                co_return std::unexpected(std::move(storage.error()));
            }
            mutation = std::move(*storage);
        }
        auto created = std::optional<ExecutionValue>();
        auto* target = static_cast<ExecutionValue*>(nullptr);
        if (receiver) {
            auto selected = located(*receiver, origin);
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            target = *selected;
        } else if (const auto element = byte_sequence_element(result_type)) {
            target = &created.emplace(
                ExecutionBytes {.type = result_type, .element = *element, .bytes = {}}
            );
        } else {
            target = &created.emplace(
                ExecutionAggregateValue {.type = result_type, .elements = {}, .capacity = 0}
            );
        }
        const auto elements = execution_slice_view(*view, &memory);
        if (!elements) {
            co_return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "slice backing is no longer alive")
            );
        }
        if (auto* bytes = std::get_if<ExecutionBytes>(target)) {
            if (auto appended = append_bytes(*bytes, *elements, origin); !appended) {
                co_return std::unexpected(std::move(appended.error()));
            }
            if (mutation) {
                auto measured = value_size(*target, origin, mutation->path.size());
                if (!measured) {
                    co_return std::unexpected(std::move(measured.error()));
                }
            }
        } else {
            if (auto checked = account_aggregate(view->extent, origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            auto& aggregate = std::get<ExecutionAggregateValue>(*target);
            const auto length = aggregate.elements.size() + view->extent;
            aggregate.elements.reserve(length);
            if (length > aggregate.capacity) {
                aggregate.capacity = std::max(length, aggregate.capacity * 2uz);
            }
            const auto element_type = *sequence_element_type(aggregate.type);
            auto appended =
                elements->visit([&](const auto children) noexcept -> ExecutionResult<void> {
                    for (auto index = 0uz; index < children.size(); ++index) {
                        auto copied = copy_value(children[index], origin, CopyMode::Value);
                        if (!copied) {
                            return std::unexpected(std::move(copied.error()));
                        }
                        if (auto typed = deliver_value(*copied, element_type, origin, 0uz);
                            !typed) {
                            return typed;
                        }
                        if (mutation) {
                            auto added = value_size(*copied, origin, mutation->path.size() + 1uz);
                            if (!added) {
                                return std::unexpected(std::move(added.error()));
                            }
                            if (auto checked = resize_value(*mutation, 0uz, 1uz + *added, origin);
                                !checked) {
                                return checked;
                            }
                        }
                        aggregate.elements.push_back(std::move(*copied));
                    }
                    return {};
                });
            if (!appended) {
                co_return std::unexpected(std::move(appended.error()));
            }
        }
        if (created) {
            if (auto* aggregate = std::get_if<ExecutionAggregateValue>(&*created)) {
                co_return constructed_value(std::move(*aggregate), origin);
            }
            co_return std::move(*created);
        }
        co_return ExecutionVoid {};
    }
    if (intrinsic == VectorIntrinsic::WithCapacity) {
        auto argument = co_await value(frame, operation.operands.front().expression);
        if (!argument) {
            co_return std::unexpected(std::move(argument.error()));
        }
        const auto element = byte_sequence_element(result_type);
        const auto maximum = element ? maximum_constant_bytes : maximum_constant_aggregate_elements;
        auto requested = offset(*argument, maximum + 1uz, origin);
        if (!requested) {
            co_return std::unexpected(std::move(requested.error()));
        }
        auto checked =
            element ? account_bytes(*requested, origin) : account_aggregate(*requested, origin);
        if (!checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (element) {
            co_return ExecutionBytes {
                .type = result_type,
                .element = *element,
                .bytes = carven::runtime::Vector<std::uint8_t>::with_capacity(*requested)
            };
        }
        auto result =
            ExecutionAggregateValue {.type = result_type, .elements = {}, .capacity = *requested};
        result.elements.reserve(*requested);
        co_return result;
    }
    if (intrinsic == VectorIntrinsic::Length || intrinsic == VectorIntrinsic::IsEmpty) {
        auto sequence =
            co_await sequence_view(frame, operation.operands.front().expression, origin);
        if (!sequence) {
            co_return std::unexpected(std::move(sequence.error()));
        }
        if (intrinsic == VectorIntrinsic::Length) {
            co_return ConstantAtom {
                .type = result_type,
                .value = IntegerConstant::from_parts(sequence->extent, false)
            };
        }
        co_return ConstantAtom {
            .type = result_type,
            .value = BooleanConstant {.value = sequence->extent == 0uz}
        };
    }
    auto receiver = co_await operand(
        frame,
        operation.operands.front().expression,
        argument_use(vector_intrinsic_contract(intrinsic).receiver_access),
        origin
    );
    if (!receiver) {
        co_return std::unexpected(std::move(receiver.error()));
    }
    auto argument = std::optional<ExecutionValue>();
    if (vector_intrinsic_contract(intrinsic).argument) {
        auto evaluated = co_await value(frame, operation.operands[1].expression);
        if (!evaluated) {
            co_return std::unexpected(std::move(evaluated.error()));
        }
        auto stored = own_storage(std::move(*evaluated), origin);
        if (!stored) {
            co_return std::unexpected(std::move(stored.error()));
        }
        argument = std::move(*stored);
    }
    const auto& receiver_place = std::get<ExecutionPlace>(*receiver);
    auto selected = located(receiver_place, origin);
    if (!selected) {
        co_return std::unexpected(std::move(selected.error()));
    }
    auto* sequence = std::get_if<ExecutionAggregateValue>(*selected);
    auto* bytes = std::get_if<ExecutionBytes>(*selected);
    if (!sequence && !bytes) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "Vector operation requires owning storage")
        );
    }
    auto mutation = std::optional<ExecutionPlace>();
    if (intrinsic == VectorIntrinsic::Push
        || intrinsic == VectorIntrinsic::Remove
        || intrinsic == VectorIntrinsic::Clear) {
        auto storage = mutation_place(receiver_place, origin);
        if (!storage) {
            co_return std::unexpected(std::move(storage.error()));
        }
        mutation = std::move(*storage);
    }
    switch (intrinsic) {
        case VectorIntrinsic::Push: {
            if (auto typed = deliver_value(
                    *argument,
                    *sequence_element_type(bytes ? bytes->type : sequence->type),
                    origin,
                    0uz
                );
                !typed) {
                co_return std::unexpected(std::move(typed.error()));
            }
            auto added = value_size(*argument, origin, mutation->path.size() + 1uz);
            if (!added) {
                co_return std::unexpected(std::move(added.error()));
            }
            if (auto checked = resize_value(*mutation, 0uz, bytes ? 0uz : 1uz + *added, origin);
                !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            if (bytes) {
                if (auto appended = append_bytes(
                        *bytes,
                        std::span<const ExecutionValue>(&*argument, 1uz),
                        origin
                    );
                    !appended) {
                    co_return std::unexpected(std::move(appended.error()));
                }
            } else {
                if (auto checked = account_aggregate(1uz, origin); !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
                if (sequence->elements.size() == sequence->capacity) {
                    sequence->capacity = sequence->capacity == 0uz ? 1uz : sequence->capacity * 2uz;
                }
                sequence->elements.push_back(std::move(*argument));
            }
            break;
        }
        case VectorIntrinsic::Remove: {
            auto index =
                offset(*argument, bytes ? bytes->bytes.size() : sequence->elements.size(), origin);
            if (!index) {
                co_return std::unexpected(std::move(index.error()));
            }
            if (bytes) {
                co_return ConstantAtom {
                    .type = result_type,
                    .value = IntegerConstant::from_parts(
                        bytes->bytes.remove(*index, carven::runtime::SourceSite::native()),
                        false
                    )
                };
            }
            auto measured =
                value_size(sequence->elements[*index], origin, mutation->path.size() + 1uz);
            if (!measured) {
                co_return std::unexpected(std::move(measured.error()));
            }
            if (auto checked = resize_value(*mutation, 1uz + *measured, 0uz, origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            auto removed = std::move(sequence->elements[*index]);
            sequence->elements.erase(sequence->elements.begin() + *index);
            co_return removed;
        }
        case VectorIntrinsic::Capacity:
            co_return ConstantAtom {
                .type = result_type,
                .value = IntegerConstant::from_parts(
                    bytes ? bytes->bytes.capacity() : sequence->capacity,
                    false
                )
            };
        case VectorIntrinsic::Reserve: {
            const auto maximum =
                bytes ? maximum_constant_bytes : maximum_constant_aggregate_elements;
            auto requested = offset(*argument, maximum + 1uz, origin);
            if (!requested) {
                co_return std::unexpected(std::move(requested.error()));
            }
            const auto capacity = bytes ? bytes->bytes.capacity() : sequence->capacity;
            if (*requested > capacity) {
                if (auto checked = bytes ? account_bytes(*requested - capacity, origin)
                                         : account_aggregate(*requested - capacity, origin);
                    !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
                if (bytes) {
                    bytes->bytes.reserve(*requested);
                } else {
                    sequence->elements.reserve(*requested);
                    sequence->capacity = *requested;
                }
            }
            break;
        }
        case VectorIntrinsic::FromSlice:
        case VectorIntrinsic::Append:
        case VectorIntrinsic::WithCapacity: std::unreachable();
        case VectorIntrinsic::Clear:
            if (sequence) {
                auto removed = value_size(**selected, origin, mutation->path.size());
                if (!removed) {
                    co_return std::unexpected(std::move(removed.error()));
                }
                if (auto checked = resize_value(*mutation, *removed, 0uz, origin); !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
            }
            if (bytes) {
                bytes->bytes.clear();
            } else {
                sequence->elements.clear();
            }
            break;
        case VectorIntrinsic::Length:
        case VectorIntrinsic::IsEmpty: std::unreachable();
    }
    co_return ExecutionVoid {};
}
