module carven:semantic.evaluation.initialization.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.simd;
import :semantic.semir.type;
import std;

auto SemanticExecutor::default_value(ConstructionTypeRef reference, ProgramOriginID origin) noexcept
    -> ExecutionTask<ExecutionValue> {
    if (auto checked = step(origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    if (const auto* term = std::get_if<TypeTermID>(&reference)) {
        const auto construction = values.construction_type_copy(*term);
        if (std::holds_alternative<ConstructionPointerTypeValue>(construction.value)) {
            co_return ExecutionPointer {.type = reference, .target = std::nullopt};
        }
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            if (auto checked = check_aggregate_size(reference, origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            const auto count = static_cast<std::size_t>(array->extent);
            if (auto checked = account_aggregate(count, origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            auto elements = std::vector<ExecutionValue>();
            elements.reserve(count);
            for (auto index = 0uz; index < count; ++index) {
                auto element = (co_await default_value(array->element, origin));
                if (!element) {
                    co_return std::unexpected(std::move(element.error()));
                }
                elements.push_back(std::move(*element));
            }
            co_return ExecutionAggregateValue {.type = reference, .elements = std::move(elements)};
        }
        co_return std::unexpected(fail(
            origin,
            ExecutionReason::Evaluation,
            "type does not support default initialization during execution"
        ));
    }
    const auto target = std::get<TypeID>(reference);
    const auto canonical = values.type_copy(target);
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
        if (builtin_is_integer(builtin->kind)) {
            co_return ConstantAtom {.type = target, .value = IntegerConstant::zero()};
        }
        if (const auto layout = simd_layout(builtin->kind)) {
            co_return ConstantAtom {
                .type = target,
                .value = SIMDConstant {.lanes = std::vector<std::uint32_t>(layout->width)}
            };
        }
        switch (builtin->kind) {
            case BuiltinType::Bool:
                co_return ConstantAtom {.type = target, .value = BooleanConstant {false}};
            case BuiltinType::Char:
                co_return ConstantAtom {.type = target, .value = CharacterConstant {U'\0'}};
            case BuiltinType::F32:
                co_return ConstantAtom {.type = target, .value = F32Constant {0.0f}};
            case BuiltinType::F64:
                co_return ConstantAtom {.type = target, .value = F64Constant {0.0}};
            case BuiltinType::String: co_return ExecutionOwnedText({});
            case BuiltinType::Str:    co_return ExecutionText(std::string());
            default:                  break;
        }
    } else if (std::holds_alternative<RangeTypeValue>(canonical.value)) {
        co_return ConstantAtom {
            .type = target,
            .value = RangeConstant {
                .begin = IntegerConstant::zero(),
                .end = IntegerConstant::zero(),
                .inclusive = false
            }
        };
    } else if (std::holds_alternative<PointerTypeValue>(canonical.value)) {
        co_return ConstantAtom {.type = target, .value = NullPointerConstant {}};
    } else if (std::holds_alternative<ArrayTypeValue>(canonical.value)
               || std::holds_alternative<StructTypeValue>(canonical.value)) {
        if (auto checked = check_aggregate_size(target, origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
        auto fields = std::optional<std::vector<TypeID>>();
        if (array == nullptr) {
            fields =
                values.struct_field_types(std::get<StructTypeValue>(canonical.value).structure);
            if (!fields) {
                co_return std::unexpected(fail(
                    origin,
                    ExecutionReason::Evaluation,
                    "default initialization requires completed fields"
                ));
            }
        }
        const auto count =
            array != nullptr ? static_cast<std::size_t>(array->extent) : fields->size();
        if (auto checked = account_aggregate(count, origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        elements.reserve(count);
        for (auto index = 0uz; index < count; ++index) {
            auto element = (co_await default_value(
                array != nullptr ? array->element : (*fields)[index],
                origin
            ));
            if (!element) {
                co_return std::unexpected(std::move(element.error()));
            }
            elements.push_back(std::move(*element));
        }
        co_return ExecutionAggregateValue {.type = target, .elements = std::move(elements)};
    }
    co_return std::unexpected(fail(
        origin,
        ExecutionReason::Evaluation,
        "type does not support default initialization during execution"
    ));
}
