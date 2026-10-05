module carven:semantic.evaluation.simd.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.operation;
import :semantic.semir.simd;
import std;

auto SemanticExecutor::simd(
    ExecutionFrame& frame,
    const SemIntrinsic& operation,
    const SemanticExpression& source
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto intrinsic = std::get<SIMDIntrinsic>(operation.operation);
    auto result_type = type(source.type.construction(), source.origin);
    if (!result_type) {
        co_return std::unexpected(std::move(result_type.error()));
    }
    auto first_type =
        type(operation.operands.front().expression.type.construction(), source.origin);
    if (!first_type) {
        co_return std::unexpected(std::move(first_type.error()));
    }
    const auto owner = simd_owner(intrinsic, *result_type, *first_type, [&](TypeID id) noexcept {
        return values.type_copy(id);
    });
    const auto layout = *simd_layout(owner);
    const auto width = layout.width;
    const auto floating = layout.element == BuiltinType::F32;
    using Lane = std::uint32_t;
    using Vector = SIMDConstant;
    const auto origin = source.origin;
    auto target = type(source.type.construction(), origin);
    if (!target) {
        co_return std::unexpected(std::move(target.error()));
    }
    if (auto budget = account_aggregate(width, origin); !budget) {
        co_return std::unexpected(std::move(budget.error()));
    }
    const auto bounds_failure = [&]() noexcept {
        return trap(
            origin,
            ExecutionReason::IndexBounds,
            "SIMD index or memory range is out of bounds"
        );
    };
    const auto scalar = [&](const ConstantFact& fact) noexcept -> Lane {
        if (floating) {
            return std::bit_cast<std::uint32_t>(std::get<F32Constant>(fact.value).value);
        } else {
            return static_cast<std::uint8_t>(std::get<IntegerConstant>(fact.value).magnitude());
        }
    };
    const auto atom = [&](Lane lane) noexcept -> ConstantAtom {
        if (floating) {
            return {
                .type = values.builtin_type(BuiltinType::F32),
                .value = F32Constant {.value = std::bit_cast<float>(lane)}
            };
        } else {
            return {
                .type = values.builtin_type(BuiltinType::U8),
                .value = IntegerConstant::from_parts(lane, false)
            };
        }
    };
    const auto is_load =
        intrinsic == SIMDIntrinsic::Load || intrinsic == SIMDIntrinsic::LoadPartial;
    if (is_load || intrinsic == SIMDIntrinsic::FromArray) {
        auto sequence = co_await sequence_view(frame, operation.operands[0].expression, origin);
        if (!sequence) {
            co_return std::unexpected(std::move(sequence.error()));
        }
        auto offset = std::uint64_t(0);
        auto fill = Lane {};
        for (auto index = 1uz; index < operation.operands.size(); ++index) {
            auto input = (co_await this->value(frame, operation.operands[index].expression));
            if (!input) {
                co_return std::unexpected(std::move(input.error()));
            }
            auto fact = read_fact(*input, origin);
            if (!fact) {
                co_return std::unexpected(std::move(fact.error()));
            }
            if (index == 1uz) {
                offset = std::get<IntegerConstant>(fact->value).magnitude();
            } else {
                fill = scalar(*fact);
            }
        }
        if (offset > sequence->extent
            || (intrinsic != SIMDIntrinsic::LoadPartial && sequence->extent - offset < width)) {
            co_return std::unexpected(bounds_failure());
        }
        auto result = Vector {.lanes = std::vector<std::uint32_t>(width)};
        std::ranges::fill(result.lanes, fill);
        const auto count = std::min<std::uint64_t>(width, sequence->extent - offset);
        for (auto index = 0uz; index < count; ++index) {
            auto selected =
                slice_element(*sequence, static_cast<std::size_t>(offset) + index, origin);
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            auto input = located(*selected, origin);
            if (!input) {
                co_return std::unexpected(std::move(input.error()));
            }
            auto fact = read_fact(**input, origin);
            if (!fact) {
                co_return std::unexpected(std::move(fact.error()));
            }
            result.lanes[index] = scalar(*fact);
        }
        co_return ConstantAtom {.type = *target, .value = result};
    }
    auto inputs = std::vector<ConstantFact>();
    for (const auto& operand : operation.operands) {
        auto input = (co_await this->value(frame, operand.expression));
        if (!input) {
            co_return std::unexpected(std::move(input.error()));
        }
        auto fact = read_fact(*input, origin);
        if (!fact) {
            co_return std::unexpected(std::move(fact.error()));
        }
        inputs.push_back(std::move(*fact));
    }
    if (intrinsic == SIMDIntrinsic::ToArray) {
        auto elements = std::vector<ExecutionValue>();
        for (const auto lane : std::get<SIMDConstant>(inputs.front().value).lanes) {
            elements.emplace_back(atom(lane));
        }
        co_return ExecutionAggregateValue {.type = *target, .elements = std::move(elements)};
    }
    auto output = evaluate_simd_constant_value(values, intrinsic, owner, inputs, *target);
    if (!output) {
        co_return std::unexpected(
            operation_failure(output.error(), origin, "unsupported SIMD operation")
        );
    }
    co_return *constant_atom(*output);
}
