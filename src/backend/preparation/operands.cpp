module carven:backend.preparation.operands.impl;

import :backend.preparation.body;
import :backend.preparation.format;
import :semantic.semir.body;
import :semantic.semir.callable;
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
    // Source Read parameters apply their value-or-borrow policy at argument
    // evaluation. Direct C++ calls retain their delegated access categories.
    const auto value_read = [&](const SemCallArgument& input,
                                bool requires_stable_storage) noexcept {
        auto prepared = argument(input);
        const auto* builtin = std::get_if<BuiltinTypeValue>(
            &semantic.types().type(input.expression.type.resolved()).value
        );
        const auto contents = semantic.type_contents(input.expression.type.resolved());
        if (input.access == AccessMode::Read
            && (requires_stable_storage || contents.contains_carven_owner())) {
            prepared.use = PreparedUse::ConstPlace;
        } else if (input.access == AccessMode::Read
                   && ((builtin != nullptr && builtin->kind != BuiltinType::String)
                       || prepared.use == PreparedUse::ReadPointer)) {
            prepared.use = PreparedUse::OperandValue;
        } else if (input.access == AccessMode::Read && prepared.use == PreparedUse::ReadBorrow) {
            prepared.use = PreparedUse::ReadArgument;
        }
        result.push_back(prepared);
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
            [](const SemUnreachable&) static noexcept {},
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
            [&](const SemTupleElement& value) noexcept {
                add(*value.source,
                    value.consumes_source() ? PreparedUse::Consume : PreparedUse::ProjectionPlace);
            },
            [&](const SemDereference& value) noexcept {
                visit_semantic_children(value, [&](const SemanticExpression& input) noexcept {
                    add(input, PreparedUse::OperandValue);
                });
            },
            [&](const SemAddressOf& value) noexcept {
                const auto* pointer = std::get_if<PointerTypeValue>(
                    &semantic.types().type(source.type.resolved()).value
                );
                if (pointer == nullptr) {
                    invariant_violation("address-of result must be a pointer");
                }
                add(*value.source,
                    pointer->access == PointerAccess::Write ? PreparedUse::WritePlace
                                                            : PreparedUse::ConstPlace);
            },
            [&](const SemIndex& value) noexcept {
                add(*value.source,
                    value.consumes_source() ? PreparedUse::Consume : PreparedUse::ProjectionPlace);
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
                const auto* prepared = std::get_if<PreparedFormat>(preparation);
                const auto writer =
                    prepared != nullptr && std::holds_alternative<PreparedWriterFormat>(*prepared);
                for (const auto& input : value.operands) {
                    if (writer) {
                        value_read(input, false);
                    } else {
                        result.push_back(argument(input));
                    }
                }
            },
            [&](const SemIntrinsic& value) noexcept {
                for (const auto& input : value.operands) {
                    value_read(input, false);
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
            [&](const SemTuple& value) noexcept {
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
                        [](const CppStaticMemberCallee<ConstructionTypeRef>&) static noexcept {},
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
            [](const SemGenericCall&) static noexcept {
                invariant_violation("unsubstituted generic call reached backend");
            },
            [&](const SemCall& value) noexcept {
                const auto closure = std::holds_alternative<ClosureTypeValue>(
                    semantic.types().type(value.callee->type.resolved()).value
                );
                add(*value.callee, closure ? PreparedUse::ConstPlace : PreparedUse::OperandValue);
                if (value.target) {
                    result.back().demand = PreparedDemand::Effects;
                }
                const auto target = value.target
                    ? value.target
                    : callable_identity(semantic, value.callee->type.resolved());
                for (const auto [index, input] : std::views::enumerate(value.arguments)) {
                    value_read(
                        input,
                        target && semantic.read_requires_stable_storage(*target, index)
                    );
                }
            },
            [&](const SemColdCall& value) noexcept {
                add(*value.callee, PreparedUse::OperandValue);
                result.back().demand = PreparedDemand::Effects;
                for (const auto [index, input] : std::views::enumerate(value.arguments)) {
                    value_read(input, semantic.read_requires_stable_storage(value.target, index));
                }
            },
            [&](const SemColdCompose& value) noexcept {
                for (const auto& input : value.inputs) {
                    add(input.expression, PreparedUse::NativeTake);
                }
            },
            [&](const SemColdTimeout& value) noexcept {
                add(*value.operation, PreparedUse::NativeTake);
                add(*value.duration_ticks, PreparedUse::OperandValue);
            },
            [&](const SemAwait& value) noexcept {
                add(*value.operand,
                    value.operand_kind == AsyncAwaitOperandKind::ColdOperation
                        ? PreparedUse::NativeTake
                        : PreparedUse::WritePlace);
            },
            [](const SemAsyncIntrinsic&) static noexcept {},
            [](const SemWaitReady&) static noexcept {},
            [](const SemCloseChild&) static noexcept {},
            [](const SemShortCircuit&) static noexcept {},
            [](const SemIf&) static noexcept {},
            [](const SemMatch&) static noexcept {},
            [](const SemTry&) static noexcept {},
            [](const SemPropagate&) static noexcept {}
        }
    );
    return result;
}
