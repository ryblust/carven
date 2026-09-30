module carven:semantic.semir.evaluation.impl;

import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.evaluation;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.simd;
import :semantic.semir.slice;
import :semantic.semir.structured;
import :semantic.semir.text;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

auto scalar(const SemIRProgram& semantic, TypeID type) noexcept -> bool {
    const auto& value = semantic.types().type(type).value;
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&value)) {
        return builtin_is_integer(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char
            || builtin->kind == BuiltinType::Str
            || simd_layout(builtin->kind).has_value();
    }
    if (const auto* enumeration = std::get_if<EnumTypeValue>(&value)) {
        return std::holds_alternative<NumericEnumRepresentation>(
            semantic.declarations().enumeration(enumeration->enumeration).representation
        );
    }
    return false;
}

} // namespace

auto known_boolean(const ConstantStore& constants, const SemanticExpression& expression) noexcept
    -> std::optional<bool> {
    if (!expression.constant) {
        return std::nullopt;
    }
    const auto* value =
        std::get_if<BooleanConstant>(&constants.constant(*expression.constant).value);
    return value == nullptr ? std::nullopt : std::optional(value->value);
}

auto known_boolean(const SemIRProgram& semantic, const SemanticExpression& expression) noexcept
    -> std::optional<bool> {
    return known_boolean(semantic.constants(), expression);
}

auto integer_operation_may_trap(
    const SemIRProgram& semantic,
    BinaryOperator operation,
    TypeID type,
    std::optional<ConstantID> right
) noexcept -> bool {
    const auto* operation_type = std::get_if<BuiltinTypeValue>(&semantic.types().type(type).value);
    if (!operation_type || !builtin_is_integer(operation_type->kind)) {
        return false;
    }
    const auto* value = right
        ? std::get_if<IntegerConstant>(&semantic.constants().constant(*right).value)
        : nullptr;
    switch (operation) {
        case BinaryOperator::Divide:
        case BinaryOperator::Remainder:  return value == nullptr || value->magnitude() == 0u;
        case BinaryOperator::LeftShift:
        case BinaryOperator::RightShift: {
            const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic.types().type(type).value);
            const auto width =
                builtin == nullptr ? std::nullopt : builtin_integer_width(builtin->kind);
            return value == nullptr || !width || value->negative() || value->magnitude() >= *width;
        }
        default: return false;
    }
}

auto evaluation_rule(const SemIRProgram& semantic, const SemanticExpression& expression) noexcept
    -> EvaluationRule {
    const auto none = EvaluationRule {.action = EvaluationAction::None, .operands = {}};
    const auto required = EvaluationRule {.action = EvaluationAction::Required, .operands = {}};
    const auto operands = [](const SemanticExpression& first,
                             const SemanticExpression* second = nullptr) static noexcept {
        return EvaluationRule {.action = EvaluationAction::Operands, .operands = {&first, second}};
    };
    return expression.value.visit(
        Overloaded {
            [&](const SemDefault&) noexcept {
                const auto& type = semantic.types().type(expression.type.resolved()).value;
                if (const auto* builtin = std::get_if<BuiltinTypeValue>(&type)) {
                    return builtin->kind == BuiltinType::String ? required : none;
                }
                return std::holds_alternative<PointerTypeValue>(type)
                        || std::holds_alternative<SliceTypeValue>(type)
                        || std::holds_alternative<RangeTypeValue>(type)
                    ? none
                    : required;
            },
            [&](const SemConstant&) noexcept { return none; },
            [&](const SemUnreachable&) noexcept { return required; },
            [&](const SemBinding&) noexcept { return none; },
            [&](const SemCallable&) noexcept { return none; },
            [&](const SemEnumConstructor&) noexcept { return none; },
            [&](const SemUnary& value) noexcept {
                return scalar(semantic, value.operand->type.resolved()) ? operands(*value.operand)
                                                                        : required;
            },
            [&](const SemBinary& value) noexcept {
                if (!scalar(semantic, value.left->type.resolved())
                    || !scalar(semantic, value.right->type.resolved())) {
                    return required;
                }
                switch (value.operation) {
                    case BinaryOperator::Divide:
                    case BinaryOperator::Remainder:
                    case BinaryOperator::LeftShift:
                    case BinaryOperator::RightShift:
                        if (!expression.constant
                            && integer_operation_may_trap(
                                semantic,
                                value.operation,
                                value.left->type.resolved(),
                                value.right->constant
                            )) {
                            return required;
                        }
                        break;
                    default: break;
                }
                return operands(*value.left, &*value.right);
            },
            [&](const SemShortCircuit& value) noexcept {
                const auto truth = known_boolean(semantic, *value.left);
                if (truth) {
                    return operands(
                        *value.left,
                        *truth == (value.operation == ShortCircuitOperator::And) ? &*value.right
                                                                                 : nullptr
                    );
                }
                return EvaluationRule {
                    .action = EvaluationAction::ShortCircuit,
                    .operands = {&*value.left, &*value.right}
                };
            },
            [&](const SemCast& value) noexcept {
                return scalar(semantic, expression.type.resolved())
                        && scalar(semantic, value.operand->type.resolved())
                    ? operands(*value.operand)
                    : required;
            },
            [&](const SemReport& value) noexcept {
                return EvaluationRule {
                    .action = EvaluationAction::Required,
                    .operands = {
                        value.condition ? &**value.condition : nullptr,
                        value.message ? &**value.message : nullptr
                    }
                };
            },
            [&](const SemPrint&) noexcept { return required; },
            [&](const SemFormat&) noexcept { return required; },
            [&](const SemIntrinsic& value) noexcept {
                return value.operation.visit(
                    Overloaded {
                        [&](const SliceIntrinsicOperation& family) noexcept {
                            switch (family.intrinsic) {
                                case SliceIntrinsic::Slice:     return required;
                                case SliceIntrinsic::FromArray:
                                case SliceIntrinsic::Len:
                                case SliceIntrinsic::IsEmpty:
                                    return operands(value.operands.front().expression);
                            }
                            std::unreachable();
                        },
                        [&](const SIMDIntrinsic& family) noexcept {
                            const auto owner = simd_owner(
                                family,
                                expression.type.resolved(),
                                value.operands.front().expression.type.resolved(),
                                [&](TypeID type) noexcept -> const CanonicalType& {
                                    return semantic.types().type(type);
                                }
                            );
                            const auto layout = *simd_layout(owner);
                            const auto number =
                                [&](std::size_t index) noexcept -> std::optional<std::uint64_t> {
                                const auto constant = value.operands[index].expression.constant;
                                if (!constant) {
                                    return std::nullopt;
                                }
                                const auto* integer = std::get_if<IntegerConstant>(
                                    &semantic.constants().constant(*constant).value
                                );
                                return integer ? integer->as_unsigned() : std::nullopt;
                            };
                            switch (family) {
                                case SIMDIntrinsic::Load:
                                case SIMDIntrinsic::LoadPartial: {
                                    const auto& sequence = value.operands.front().expression;
                                    auto extent = std::optional<std::uint64_t>();
                                    if (const auto* operation =
                                            std::get_if<SemIntrinsic>(&sequence.value)) {
                                        if (const auto* slice =
                                                std::get_if<SliceIntrinsicOperation>(
                                                    &operation->operation
                                                )) {
                                            extent = slice->result_extent;
                                        }
                                    }
                                    if (sequence.constant) {
                                        const auto children = constant_children(
                                            semantic.constants().constant(*sequence.constant).value
                                        );
                                        if (children) {
                                            extent = children->size();
                                        }
                                    }
                                    const auto offset = number(1);
                                    if (!extent
                                        || !offset
                                        || *offset > *extent
                                        || (family == SIMDIntrinsic::Load
                                            && *extent - *offset < layout.width)) {
                                        return required;
                                    }
                                    break;
                                }
                                case SIMDIntrinsic::Lane:
                                case SIMDIntrinsic::WithLane: {
                                    const auto index = number(1);
                                    if (!index || *index >= layout.width) {
                                        return required;
                                    }
                                    break;
                                }
                                case SIMDIntrinsic::Prefix: {
                                    const auto count = number(0);
                                    if (!count || *count > layout.width) {
                                        return required;
                                    }
                                    break;
                                }
                                default: break;
                            }
                            auto rule = EvaluationRule {
                                .action = EvaluationAction::Operands,
                                .operands = {}
                            };
                            for (const auto& [index, operand] :
                                 std::views::enumerate(value.operands)) {
                                rule.operands[index] = &operand.expression;
                            }
                            return rule;
                        },
                        [&](const TextIntrinsic& family) noexcept {
                            return text_intrinsic_writes(family) || family == TextIntrinsic::FromStr
                                ? required
                                : operands(value.operands.front().expression);
                        }
                    }
                );
            },
            [&](const SemDereference&) noexcept { return required; },
            [&](const SemAddressOf&) noexcept { return required; },
            [&](const SemField& value) noexcept { return operands(*value.source); },
            [&](const SemIndex& value) noexcept {
                return std::holds_alternative<RuntimeCheckedBounds>(value.bounds)
                    ? required
                    : operands(*value.source, &*value.index);
            },
            [&](const SemRange& value) noexcept {
                return operands(*value.begin, std::addressof(*value.end));
            },
            [&](const SemArray&) noexcept { return required; },
            [&](const SemArrayAdopt&) noexcept { return required; },
            [&](const SemStruct&) noexcept { return required; },
            [&](const SemEnumCase&) noexcept { return required; },
            [&](const SemCpp&) noexcept { return required; },
            [&](const SemCppCall&) noexcept { return required; },
            [&](const SemCall&) noexcept { return required; },
            [&](const SemClosure&) noexcept { return required; },
            [&](const SemBorrowCallable& value) noexcept { return operands(*value.source); },
            [&](const SemTake&) noexcept { return required; },
            [&](const SemPropagate&) noexcept { return required; },
            [&](const SemIf&) noexcept { return required; },
            [&](const SemMatch&) noexcept { return required; },
            [&](const SemTry&) noexcept { return required; }
        }
    );
}
