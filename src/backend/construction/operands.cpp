module carven:backend.construction.operands.impl;

import :backend.construction.builder;
import :backend.construction;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyConstructionBuilder::operands(const SemanticExpression& source) noexcept
    -> std::vector<ConstructionOperand> {
    auto result = std::vector<ConstructionOperand>();
    const auto add = [&](const SemanticExpression& input, ConstructionUse use) noexcept {
        result.push_back(operand(input, use));
    };
    const auto receiver = [&](const SemanticExpression& input, AccessMode access) noexcept {
        add(input,
            access == AccessMode::Write      ? ConstructionUse::Place
                : access == AccessMode::Take ? ConstructionUse::NativeTake
                                             : ConstructionUse::ConstPlace);
    };
    const auto native = [&](const SemCallArgument& input) noexcept {
        auto prepared = argument(input);
        if (input.access == AccessMode::Take) {
            prepared.use = ConstructionUse::NativeTake;
        }
        result.push_back(prepared);
    };
    std::visit(
        Overloaded {
            [](const SemConstant&) static noexcept {},
            [](const SemBinding&) static noexcept {},
            [](const SemCallable&) static noexcept {},
            [](const SemEnumConstructor&) static noexcept {},
            [&](const SemUnary& value) noexcept {
                add(*value.operand, ConstructionUse::OperandValue);
            },
            [&](const SemBinary& value) noexcept {
                add(*value.left, ConstructionUse::OperandValue);
                add(*value.right, ConstructionUse::OperandValue);
            },
            [&](const SemCast& value) noexcept {
                add(*value.operand, ConstructionUse::OperandValue);
            },
            [&](const SemField& value) noexcept { add(*value.source, ConstructionUse::Place); },
            [&](const SemDereference& value) noexcept {
                add(*value.source, ConstructionUse::AddressValue);
            },
            [&](const SemIndex& value) noexcept {
                add(*value.source, ConstructionUse::Place);
                add(*value.index, ConstructionUse::OperandValue);
            },
            [&](const SemFormat& value) noexcept {
                for (const auto& input : value.operands) {
                    result.push_back(argument(input));
                }
            },
            [&](const SemSliceIntrinsic& value) noexcept {
                for (const auto& input : value.operands) {
                    result.push_back(argument(input));
                }
            },
            [&](const SemTextIntrinsic& value) noexcept {
                for (const auto& input : value.operands) {
                    result.push_back(argument(input));
                }
            },
            [&](const SemArray& value) noexcept {
                for (const auto& element : value.elements) {
                    add(element, ConstructionUse::Consume);
                }
            },
            [&](const SemArrayAdopt& value) noexcept {
                add(*value.source, ConstructionUse::Place);
            },
            [&](const SemStruct& value) noexcept {
                for (const auto& field : value.fields) {
                    add(field.value, ConstructionUse::Consume);
                }
            },
            [&](const SemEnumCase& value) noexcept {
                for (const auto& element : value.payload) {
                    add(element, ConstructionUse::Consume);
                }
            },
            [&](const SemClosure& value) noexcept {
                for (const auto& capture : value.captures) {
                    add(capture.expression,
                        capture.mode == CaptureMode::Write ? ConstructionUse::Place
                                                           : ConstructionUse::Consume);
                }
            },
            [&](const SemBorrowCallable& value) noexcept {
                add(*value.source, ConstructionUse::Place);
            },
            [&](const SemTake& value) noexcept { add(*value.place, ConstructionUse::Place); },
            [&](const SemCpp& value) noexcept {
                for (const auto& input : value.operands) {
                    if (result.empty()
                        && (std::holds_alternative<CppMemberOperation>(value.operation)
                            || std::holds_alternative<CppIndexOperation>(value.operation))) {
                        receiver(input.expression, input.access);
                    } else {
                        native(input);
                    }
                }
            },
            [&](const SemCppCall& value) noexcept {
                std::visit(
                    Overloaded {
                        [](const CppNameReference&) static noexcept {},
                        [&](const CppMemberCallee<SemCppOperand>& member) noexcept {
                            receiver(*member.receiver.expression, member.receiver.access);
                        },
                        [&](const SemCppOperand& callee) noexcept {
                            receiver(*callee.expression, callee.access);
                        }
                    },
                    value.callee
                );
                for (const auto& input : value.arguments) {
                    native(input);
                }
            },
            [&](const SemCall& value) noexcept {
                const auto closure = std::holds_alternative<ClosureTypeValue>(
                    semantic.types().type(value.callee->type.resolved()).value
                );
                add(*value.callee,
                    closure ? ConstructionUse::ConstPlace : ConstructionUse::OperandValue);
                for (const auto& input : value.arguments) {
                    result.push_back(argument(input));
                }
            },
            [](const SemShortCircuit&) static noexcept {
                invariant_violation("short circuit has execution children");
            },
            [](const SemIf&) static noexcept {
                invariant_violation("conditional has execution children");
            },
            [](const SemMatch&) static noexcept {
                invariant_violation("match has execution children");
            },
            [](const SemTry&) static noexcept {
                invariant_violation("try has execution children");
            },
            [](const SemPropagate&) static noexcept {
                invariant_violation("propagation forwards its operation");
            }
        },
        source.value
    );
    return result;
}

auto construction_operands(const ConstructionExpression& source) noexcept
    -> std::span<const ConstructionOperand> {
    const auto* operation = std::get_if<ConstructionOperation>(&source.value);
    return operation == nullptr ? std::span<const ConstructionOperand>() : operation->operands;
}
