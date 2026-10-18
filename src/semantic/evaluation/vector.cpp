module carven:semantic.evaluation.vector.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.semir.vector;
import std;

auto SemanticExecutor::vector_intrinsic(
    ExecutionFrame& frame,
    const SemIntrinsic& operation,
    TypeID result_type,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto intrinsic = std::get<VectorIntrinsicOperation>(operation.operation).intrinsic;
    if (intrinsic == VectorIntrinsic::FromSlice) {
        auto view = co_await sequence_view(frame, operation.operands.front().expression, origin);
        if (!view) {
            co_return std::unexpected(std::move(view.error()));
        }
        if (auto checked = account_aggregate(view->extent, origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto result =
            ExecutionAggregateValue {.type = result_type, .elements = {}, .capacity = view->extent};
        result.elements.reserve(view->extent);
        for (auto index = 0uz; index < view->extent; ++index) {
            auto place = slice_element(*view, index, origin);
            if (!place) {
                co_return std::unexpected(std::move(place.error()));
            }
            auto element = located(*place, origin);
            if (!element) {
                co_return std::unexpected(std::move(element.error()));
            }
            auto copied = copy_value(**element, origin, CopyMode::Value);
            if (!copied) {
                co_return std::unexpected(std::move(copied.error()));
            }
            auto owned = own_storage(std::move(*copied), origin);
            if (!owned) {
                co_return std::unexpected(std::move(owned.error()));
            }
            result.elements.push_back(std::move(*owned));
        }
        co_return bounded_value(std::move(result), origin);
    }
    if (intrinsic == VectorIntrinsic::WithCapacity) {
        auto argument = co_await value(frame, operation.operands.front().expression);
        if (!argument) {
            co_return std::unexpected(std::move(argument.error()));
        }
        auto requested = offset(*argument, maximum_constant_aggregate_elements + 1uz, origin);
        if (!requested) {
            co_return std::unexpected(std::move(requested.error()));
        }
        if (auto checked = account_aggregate(*requested, origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
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
    if (!sequence) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "Vector operation requires owning storage")
        );
    }
    switch (intrinsic) {
        case VectorIntrinsic::Push:
            if (auto checked = account_aggregate(1uz, origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            if (sequence->elements.size() == sequence->capacity) {
                sequence->capacity = sequence->capacity == 0uz ? 1uz : sequence->capacity * 2uz;
            }
            sequence->elements.push_back(std::move(*argument));
            if (const auto* root = memory.owning_value(receiver_place)) {
                if (auto checked = check_value_size(*root, origin); !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
            }
            break;
        case VectorIntrinsic::Remove: {
            auto index = offset(*argument, sequence->elements.size(), origin);
            if (!index) {
                co_return std::unexpected(std::move(index.error()));
            }
            auto removed = std::move(sequence->elements[*index]);
            sequence->elements.erase(sequence->elements.begin() + *index);
            co_return removed;
        }
        case VectorIntrinsic::Capacity:
            co_return ConstantAtom {
                .type = result_type,
                .value = IntegerConstant::from_parts(sequence->capacity, false)
            };
        case VectorIntrinsic::Reserve: {
            auto requested = offset(*argument, maximum_constant_aggregate_elements + 1uz, origin);
            if (!requested) {
                co_return std::unexpected(std::move(requested.error()));
            }
            if (*requested > sequence->capacity) {
                if (auto checked = account_aggregate(*requested - sequence->capacity, origin);
                    !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
                sequence->elements.reserve(*requested);
                sequence->capacity = *requested;
            }
            break;
        }
        case VectorIntrinsic::FromSlice:
        case VectorIntrinsic::WithCapacity: std::unreachable();
        case VectorIntrinsic::Clear:        sequence->elements.clear(); break;
        case VectorIntrinsic::Length:
        case VectorIntrinsic::IsEmpty:      std::unreachable();
    }
    co_return ExecutionVoid {};
}
