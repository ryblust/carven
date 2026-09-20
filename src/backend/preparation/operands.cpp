module carven:backend.preparation.operands.impl;

import :backend.preparation.body;
import :semantic.semir.body;
import :semantic.semir.children;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyPreparation::operands(
    const SemanticExpression& source,
    const OperationPreparation* preparation
) const noexcept -> std::vector<PreparedOperand> {
    auto result = std::vector<PreparedOperand>();
    const auto add = [&](const SemanticExpression& input, PreparedUse use) noexcept {
        result.push_back(operand(input, use));
    };
    const auto receiver = [&](const SemanticExpression& input, AccessMode access) noexcept {
        add(input,
            access == AccessMode::Write      ? PreparedUse::WritePlace
                : access == AccessMode::Take ? PreparedUse::NativeTake
                                             : PreparedUse::ConstPlace);
    };
    const auto native = [&](const SemCallArgument& input) noexcept {
        auto prepared = argument(input);
        if (input.access == AccessMode::Take) {
            prepared.use = PreparedUse::NativeTake;
        }
        result.push_back(prepared);
    };
    source.value.visit(
        Overloaded {
            [](const SemDefault&) static noexcept {},
            [](const SemConstant&) static noexcept {},
            [](const SemBinding&) static noexcept {},
            [](const SemCallable&) static noexcept {},
            [](const SemEnumConstructor&) static noexcept {},
            [&](const SemUnary& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::OperandValue);
                });
            },
            [&](const SemBinary& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::OperandValue);
                });
            },
            [&](const SemCast& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::OperandValue);
                });
            },
            [&](const SemField& value) noexcept {
                add(*value.source,
                    value.consumes_source() ? PreparedUse::Consume : PreparedUse::ProjectionPlace);
            },
            [&](const SemDereference& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::AddressValue);
                });
            },
            [&](const SemIndex& value) noexcept {
                add(*value.source, PreparedUse::ProjectionPlace);
                add(*value.index, PreparedUse::OperandValue);
            },
            [](const SemReport&) static noexcept {},
            [&](const SemPrint& value) noexcept {
                for (const auto& input : value.operands) {
                    result.push_back(argument(input));
                }
            },
            [&](const SemFormat& value) noexcept {
                if (value.receiver) {
                    add(**value.receiver, PreparedUse::WritePlace);
                }
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
            [&](const SemRange& value) noexcept {
                add(*value.begin, PreparedUse::OperandValue);
                add(*value.end, PreparedUse::OperandValue);
            },
            [&](const SemArray& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::Consume);
                });
            },
            [&](const SemArrayAdopt& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::ConstPlace);
                });
            },
            [&](const SemStruct& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::Consume);
                });
            },
            [&](const SemEnumCase& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::Consume);
                });
            },
            [&](const SemClosure& value) noexcept {
                for (const auto& capture : value.captures) {
                    add(capture.expression,
                        capture.mode == CaptureMode::Write ? PreparedUse::WritePlace
                                                           : PreparedUse::Consume);
                }
            },
            [&](const SemBorrowCallable& value) noexcept {
                const auto* plan = std::get_if<PreparedCallableAdaptation>(preparation);
                if (plan == nullptr) {
                    invariant_violation("callable adaptation requires preparation");
                }
                add(*value.source,
                    plan->adaptation.borrows_storage() ? PreparedUse::ConstPlace
                                                       : PreparedUse::Consume);
            },
            [&](const SemTake& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::WritePlace);
                });
            },
            [&](const SemCpp& value) noexcept {
                if (const auto* conversion = std::get_if<CppConvertOperation>(&value.operation);
                    conversion != nullptr
                    && !conversion->explicit_cast
                    && source.category == SemanticValueCategory::Value) {
                    // Value initialization delivers the value into its declared
                    // type; it is not a native call borrowing a Read argument.
                    add(value.operands.front().expression, PreparedUse::Consume);
                    return;
                }
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
                value.callee.visit(
                    Overloaded {
                        [](const CppNameReference&) static noexcept {},
                        [&](const CppMemberCallee<SemCppOperand>& member) noexcept {
                            receiver(*member.receiver.expression, member.receiver.access);
                        },
                        [&](const SemCppOperand& callee) noexcept {
                            receiver(*callee.expression, callee.access);
                        }
                    }
                );
                for (const auto& input : value.arguments) {
                    native(input);
                }
            },
            [&](const SemCall& value) noexcept {
                const auto closure = std::holds_alternative<ClosureTypeValue>(
                    semantic.types().type(value.callee->type.resolved()).value
                );
                add(*value.callee, closure ? PreparedUse::ConstPlace : PreparedUse::OperandValue);
                if (value.target) {
                    result.back().demand = PreparedDemand::Effects;
                }
                for (const auto& input : value.arguments) {
                    auto prepared = argument(input);
                    const auto* builtin = std::get_if<BuiltinTypeValue>(
                        &semantic.types().type(input.expression.type.resolved()).value
                    );
                    if (input.access == AccessMode::Read
                        && builtin != nullptr
                        && builtin->kind != BuiltinType::String
                        && builtin->kind != BuiltinType::EntryArgs) {
                        prepared.use = PreparedUse::OperandValue;
                    }
                    result.push_back(prepared);
                }
            },
            [](const SemShortCircuit&) static noexcept {},
            [](const SemIf&) static noexcept {},
            [](const SemMatch&) static noexcept {},
            [](const SemTry&) static noexcept {},
            [](const SemPropagate&) static noexcept {}
        }
    );
    return result;
}
