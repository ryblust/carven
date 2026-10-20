module carven:semantic.evaluation.initialization.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.simd;
import :semantic.semir.type;
import std;

auto SemanticExecutor::default_value(ConstructionTypeRef reference, ProgramOriginID origin) noexcept
    -> ExecutionTask<ExecutionValue> {
    reference = execution_type_reference(values, reference);
    if (auto checked = step(origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    const auto pointer =
        visit_execution_type(values, reference, [](const auto& shape) static noexcept -> bool {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            return std::same_as<Shape, PointerTypeValue> || std::same_as<Shape, GenericPointerType>;
        });
    if (pointer) {
        co_return ExecutionPointer {.type = reference, .target = std::nullopt};
    }
    const auto array = visit_execution_type(
        values,
        reference,
        [](const auto& shape) static noexcept -> std::optional<ConstructionArrayTypeValue> {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, ArrayTypeValue>
                          || std::same_as<Shape, ConstructionArrayTypeValue>
                          || std::same_as<Shape, GenericArrayType>) {
                return ConstructionArrayTypeValue {
                    .element = shape.element,
                    .extent = shape.extent
                };
            }
            return std::nullopt;
        }
    );
    if (array) {
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
            auto element = co_await default_value(array->element, origin);
            if (!element) {
                co_return std::unexpected(std::move(element.error()));
            }
            elements.push_back(std::move(*element));
        }
        co_return constructed_value(
            ExecutionAggregateValue {
                .type = reference,
                .elements = std::move(elements),
                .capacity = 0uz
            },
            origin
        );
    }
    const auto* concrete = std::get_if<TypeID>(&reference);
    if (!concrete) {
        co_return std::unexpected(fail(
            origin,
            ExecutionReason::Evaluation,
            "type does not support default initialization during execution"
        ));
    }
    const auto target = *concrete;
    const auto canonical = values.type_copy(target);
    if (std::holds_alternative<VectorTypeValue>(canonical.value)) {
        co_return constructed_value(
            ExecutionAggregateValue {.type = target, .elements = {}, .capacity = 0uz},
            origin
        );
    }
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
        if (builtin_is_integer(builtin->kind)) {
            co_return ConstantAtom {.type = target, .value = IntegerConstant::zero()};
        }
        if (const auto layout = simd_layout(builtin->kind)) {
            co_return ConstantAtom {.type = target, .value = SIMDConstant {}};
        }
        switch (builtin->kind) {
            case BuiltinType::Bool:
                co_return ConstantAtom {.type = target, .value = BooleanConstant {.value = false}};
            case BuiltinType::Char:
                co_return ConstantAtom {
                    .type = target,
                    .value = CharacterConstant {.scalar = U'\0'}
                };
            case BuiltinType::F32:
                co_return ConstantAtom {.type = target, .value = F32Constant {.value = 0.0f}};
            case BuiltinType::F64:
                co_return ConstantAtom {.type = target, .value = F64Constant {.value = 0.0}};
            case BuiltinType::Void:   co_return ExecutionVoid {};
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
    } else if (std::holds_alternative<StructTypeValue>(canonical.value)
               || std::holds_alternative<TupleTypeValue>(canonical.value)) {
        if (auto checked = check_aggregate_size(target, origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        const auto* tuple = std::get_if<TupleTypeValue>(&canonical.value);
        const auto fields = tuple
            ? std::optional(tuple->elements)
            : values.struct_field_types(std::get<StructTypeValue>(canonical.value).structure);
        if (!fields) {
            co_return std::unexpected(fail(
                origin,
                ExecutionReason::Evaluation,
                "default initialization requires completed fields"
            ));
        }
        const auto count = fields->size();
        if (auto checked = account_aggregate(count, origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        elements.reserve(count);
        for (auto index = 0uz; index < count; ++index) {
            auto element = (co_await default_value((*fields)[index], origin));
            if (!element) {
                co_return std::unexpected(std::move(element.error()));
            }
            elements.push_back(std::move(*element));
        }
        co_return constructed_value(
            ExecutionAggregateValue {
                .type = target,
                .elements = std::move(elements),
                .capacity = 0uz
            },
            origin
        );
    }
    co_return std::unexpected(fail(
        origin,
        ExecutionReason::Evaluation,
        "type does not support default initialization during execution"
    ));
}
