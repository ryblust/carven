module carven:semantic.evaluation.sequence.impl;

import :semantic.evaluation.executor;
import :semantic.semir.sequence;
import std;

auto SemanticExecutor::sequence_intrinsic(
    ExecutionFrame& frame,
    const SemIntrinsic& operation,
    TypeID result_type,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto intrinsic = std::get<SequenceIntrinsicOperation>(operation.operation).intrinsic;
    if (intrinsic == SequenceIntrinsic::Len || intrinsic == SequenceIntrinsic::IsEmpty) {
        auto sequence =
            co_await sequence_view(frame, operation.operands.front().expression, origin);
        if (!sequence) {
            co_return std::unexpected(std::move(sequence.error()));
        }
        if (intrinsic == SequenceIntrinsic::Len) {
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
    auto receiver = co_await place(frame, operation.operands.front().expression);
    if (!receiver) {
        co_return std::unexpected(std::move(receiver.error()));
    }
    auto argument = std::optional<ExecutionValue>();
    if (intrinsic != SequenceIntrinsic::Clear) {
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
    auto selected = located(*receiver, origin);
    if (!selected) {
        co_return std::unexpected(std::move(selected.error()));
    }
    auto* sequence = std::get_if<ExecutionAggregateValue>(*selected);
    if (!sequence) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "Sequence operation requires owning storage")
        );
    }
    switch (intrinsic) {
        case SequenceIntrinsic::Push:
            if (auto checked = account_aggregate(1uz, origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            sequence->elements.push_back(std::move(*argument));
            if (const auto* root = memory.owning_value(*receiver)) {
                if (auto checked = check_value_size(*root, origin); !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
            }
            break;
        case SequenceIntrinsic::Remove: {
            auto index = offset(*argument, sequence->elements.size(), origin);
            if (!index) {
                co_return std::unexpected(std::move(index.error()));
            }
            sequence->elements.erase(sequence->elements.begin() + *index);
            break;
        }
        case SequenceIntrinsic::Clear:   sequence->elements.clear(); break;
        case SequenceIntrinsic::Len:
        case SequenceIntrinsic::IsEmpty: std::unreachable();
    }
    co_return ExecutionVoid {};
}
