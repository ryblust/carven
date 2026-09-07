module carven:semantic.semir.evaluation.impl;

import :semantic.semir;
import :semantic.semir.evaluation;
import :semantic.semir.traversal;
import :support.visit;
import std;

namespace {
auto scalar(const SemIRProgram& semantic, TypeID type) noexcept -> bool {
    const auto& value = semantic.types().type(type).value;
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&value)) {
        return builtin_is_integer(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char
            || builtin->kind == BuiltinType::Str;
    }
    if (const auto* enumeration = std::get_if<EnumTypeValue>(&value)) {
        return std::holds_alternative<NumericEnumRepresentation>(
            semantic.declarations().enumeration(enumeration->enumeration).representation
        );
    }
    return false;
}
} // namespace

auto known_boolean(const SemIRProgram& semantic, const SemanticExpression& expression) noexcept
    -> std::optional<bool> {
    if (!expression.constant) {
        return std::nullopt;
    }
    const auto* value =
        std::get_if<BooleanConstant>(&semantic.constants().constant(*expression.constant).value);
    return value == nullptr ? std::nullopt : std::optional(value->value);
}

auto evaluation_rule(const SemIRProgram& semantic, const SemanticExpression& expression) noexcept
    -> EvaluationRule {
    const auto none = EvaluationRule {.action = EvaluationAction::None, .operands = {}};
    const auto required = EvaluationRule {.action = EvaluationAction::Required, .operands = {}};
    const auto operands = [](const SemanticExpression& first,
                             const SemanticExpression* second = nullptr) static noexcept {
        return EvaluationRule {.action = EvaluationAction::Operands, .operands = {&first, second}};
    };
    return std::visit(
        Overloaded {
            [&](const SemConstant&) noexcept { return none; },
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
                        if (!expression.constant) {
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
            [&](const SemTextIntrinsic& value) noexcept { return operands(*value.source); },
            [&](const SemField& value) noexcept { return operands(*value.source); },
            [&](const SemIndex& value) noexcept {
                return std::holds_alternative<RuntimeCheckedBounds>(value.bounds)
                    ? required
                    : operands(*value.source, &*value.index);
            },
            [&](const SemArray&) noexcept { return required; },
            [&](const SemArrayAdopt&) noexcept { return required; },
            [&](const SemStruct&) noexcept { return required; },
            [&](const SemEnumCase&) noexcept { return required; },
            [&](const SemCpp& value) noexcept {
                return std::holds_alternative<CppCStringOperation>(value.operation) ? none
                                                                                    : required;
            },
            [&](const SemCppCall&) noexcept { return required; },
            [&](const SemCall&) noexcept { return required; },
            [&](const SemClosure&) noexcept { return required; },
            [&](const SemBorrowCallable&) noexcept { return required; },
            [&](const SemTake&) noexcept { return required; },
            [&](const SemPropagate&) noexcept { return required; },
            [&](const SemIf&) noexcept { return required; },
            [&](const SemMatch&) noexcept { return required; },
            [&](const SemTry&) noexcept { return required; }
        },
        expression.value
    );
}

auto evaluation_requires_execution(
    const SemIRProgram& semantic,
    const SemanticExpression& expression
) noexcept -> bool {
    const auto rule = evaluation_rule(semantic, expression);
    return rule.action == EvaluationAction::Required
        || std::ranges::any_of(rule.operands, [&](const auto* operand) noexcept {
               return operand != nullptr && evaluation_requires_execution(semantic, *operand);
           });
}

auto evaluation_reads_storage(
    const SemIRProgram& semantic,
    const SemanticExpression& expression
) noexcept -> bool {
    if (std::holds_alternative<SemBinding>(expression.value)) {
        return true;
    }
    const auto rule = evaluation_rule(semantic, expression);
    return rule.action == EvaluationAction::Required
        || std::ranges::any_of(rule.operands, [&](const auto* operand) noexcept {
               return operand != nullptr && evaluation_reads_storage(semantic, *operand);
           });
}

auto evaluation_preserves_full_expression(
    const SemIRProgram& semantic,
    const SemanticExpression& expression
) noexcept -> bool {
    const auto rule = evaluation_rule(semantic, expression);
    if (rule.action != EvaluationAction::Required) {
        return std::ranges::any_of(rule.operands, [&](const auto* operand) noexcept {
            return operand != nullptr && evaluation_preserves_full_expression(semantic, *operand);
        });
    }
    auto preserve = false;
    visit_semantic_nodes(expression, [&](const SemanticExpression& value) noexcept {
        const auto* builtin =
            std::get_if<BuiltinTypeValue>(&semantic.types().type(value.type.resolved()).value);
        if (builtin == nullptr
            && !scalar(semantic, value.type.resolved())
            && !std::holds_alternative<SemCallable>(value.value)
            && !std::holds_alternative<SemEnumConstructor>(value.value)) {
            preserve = true;
        }
    });
    return preserve;
}
