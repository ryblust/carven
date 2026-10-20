module carven:semantic.evaluation.simd.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.operation;
import :semantic.evaluation.value;
import :semantic.semir.simd;
import std;

auto SemanticExecutor::simd(
    ExecutionFrame& frame,
    const SemIntrinsic& operation,
    const SemanticExpression& source
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto intrinsic = std::get<SIMDIntrinsic>(operation.operation);
    auto target = type(source.type.construction(), source.origin);
    if (!target) {
        co_return std::unexpected(std::move(target.error()));
    }
    auto first_type =
        type(operation.operands.front().expression.type.construction(), source.origin);
    if (!first_type) {
        co_return std::unexpected(std::move(first_type.error()));
    }
    const auto owner = simd_owner(intrinsic, *target, *first_type, [&](TypeID id) noexcept {
        return values.type_copy(id);
    });
    const auto layout = *simd_layout(owner);
    const auto width = layout.width;
    const auto floating = layout.element == BuiltinType::F32;
    using Lane = std::uint32_t;
    using Vector = SIMDConstant;
    const auto origin = source.origin;
    const auto result_shape = values.type_copy(*target);
    auto result_elements = std::uint64_t(0);
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&result_shape.value)) {
        if (const auto result_layout = simd_layout(builtin->kind)) {
            result_elements = result_layout->width;
        }
    } else if (const auto* array = std::get_if<ArrayTypeValue>(&result_shape.value)) {
        result_elements = array->extent;
    }
    if (auto budget = account_aggregate(result_elements, origin); !budget) {
        co_return std::unexpected(std::move(budget.error()));
    }
    const auto bounds_failure = [&]() noexcept {
        return trap(
            origin,
            ExecutionReason::IndexBounds,
            "SIMD index or memory range is out of bounds"
        );
    };
    const auto scalar = [&](const ConstantAtom& fact) noexcept -> Lane {
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
            auto fact = read_atom(*input, origin);
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
        const auto elements = execution_slice_view(*sequence, &memory);
        if (!elements) {
            co_return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "slice backing is no longer alive")
            );
        }
        auto result = Vector {};
        for (auto index = 0uz; index < width; ++index) {
            result.set_lane(index, layout.element, fill);
        }
        const auto count = std::min<std::uint64_t>(width, sequence->extent - offset);
        auto loaded = elements->visit([&](const auto children) noexcept -> ExecutionResult<void> {
            for (auto index = 0uz; index < count; ++index) {
                const auto position = static_cast<std::size_t>(offset) + index;
                if constexpr (std::same_as<
                                  std::remove_cvref_t<decltype(children)>,
                                  ExecutionByteView>) {
                    result.set_lane(
                        index,
                        layout.element,
                        static_cast<unsigned char>(children.bytes[position])
                    );
                } else {
                    auto fact = read_atom(children[position], origin);
                    if (!fact) {
                        return std::unexpected(std::move(fact.error()));
                    }
                    result.set_lane(index, layout.element, scalar(*fact));
                }
            }
            return {};
        });
        if (!loaded) {
            co_return std::unexpected(std::move(loaded.error()));
        }
        co_return ConstantAtom {.type = *target, .value = result};
    }
    if (intrinsic == SIMDIntrinsic::Swizzle) {
        auto input = co_await value(frame, operation.operands[0].expression);
        if (!input) {
            co_return std::unexpected(std::move(input.error()));
        }
        auto fact = read_atom(*input, origin);
        if (!fact) {
            co_return std::unexpected(std::move(fact.error()));
        }
        const auto& lanes = std::get<Vector>(fact->value);
        auto indices = co_await sequence_view(frame, operation.operands[1].expression, origin);
        if (!indices) {
            co_return std::unexpected(std::move(indices.error()));
        }
        auto result = Vector {};
        for (auto index = 0uz; index < width; ++index) {
            auto selected = slice_element(*indices, index, origin);
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            auto control = observed(*selected, origin);
            if (!control) {
                co_return std::unexpected(std::move(control.error()));
            }
            auto control_fact = read_atom(control->get(), origin);
            if (!control_fact) {
                co_return std::unexpected(std::move(control_fact.error()));
            }
            const auto position = std::get<IntegerConstant>(control_fact->value).magnitude();
            if (position >= width) {
                co_return std::unexpected(bounds_failure());
            }
            result.set_lane(index, layout.element, lanes.lane(position, layout.element));
        }
        co_return ConstantAtom {.type = *target, .value = result};
    }
    auto inputs = std::vector<ConstantAtom>();
    for (const auto& operand : operation.operands) {
        auto input = (co_await this->value(frame, operand.expression));
        if (!input) {
            co_return std::unexpected(std::move(input.error()));
        }
        auto fact = read_atom(*input, origin);
        if (!fact) {
            co_return std::unexpected(std::move(fact.error()));
        }
        inputs.push_back(*fact);
    }
    if (intrinsic == SIMDIntrinsic::ToArray) {
        auto elements = std::vector<ExecutionValue>();
        for (const auto lane :
             std::get<SIMDConstant>(inputs.front().value).lanes(layout.element, width)) {
            elements.emplace_back(atom(lane));
        }
        co_return ExecutionAggregateValue {
            .type = *target,
            .elements = std::move(elements),
            .capacity = 0uz
        };
    }
    auto output = evaluate_simd_constant_value(values, intrinsic, owner, inputs, *target);
    if (!output) {
        co_return std::unexpected(
            operation_failure(output.error(), origin, "unsupported SIMD operation")
        );
    }
    co_return *output;
}
