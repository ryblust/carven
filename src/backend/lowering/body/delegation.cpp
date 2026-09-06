module carven:backend.lowering.body.delegation.impl;

import :backend.lowering.body.lowerer;
import :support.visit;
import std;

namespace body_lowering {

auto BodyLowerer::cpp_call(const SemCppCall& call, StatementSequence& destination) noexcept
    -> std::optional<TargetExpr> {
    const auto receiver = [&](const SemCppOperand& value) noexcept {
        return operand(
            *value.expression,
            destination,
            value.access == AccessMode::Write ? OperandUse::Place : OperandUse::ConstPlace
        );
    };
    auto callee = std::visit(
        Overloaded {
            [&](const CppNameReference& name) noexcept -> std::optional<TargetExpr> {
                return name_expression(context.cpp_name(name));
            },
            [&](const CppMemberCallee<SemCppOperand>& member) noexcept {
                return receiver(member.receiver).transform([&](TargetExpr value) noexcept {
                    return member_expression(
                        std::move(value),
                        TargetIdentifier::from_spelling(member.member)
                    );
                });
            },
            [&](const SemCppOperand& value) noexcept { return receiver(value); }
        },
        call.callee
    );
    if (!callee) {
        return std::nullopt;
    }
    auto sources = std::vector<Operand>();
    for (const auto& argument : call.arguments) {
        sources.push_back(
            {argument.expression,
             argument.access == AccessMode::Write      ? OperandUse::Place
                 : argument.access == AccessMode::Take ? OperandUse::Own
                                                       : OperandUse::Read}
        );
    }
    auto arguments = operands(sources, destination);
    if (!arguments) {
        return std::nullopt;
    }
    return call_expression(std::move(*callee), std::move(*arguments));
}

auto BodyLowerer::cpp_operation(
    const SemanticExpression& source,
    const SemCpp& value,
    StatementSequence& destination
) noexcept -> std::optional<TargetExpr> {
    if (const auto* name = std::get_if<CppNameOperation>(&value.operation)) {
        return name_expression(context.cpp_name(name->name));
    }
    if (std::holds_alternative<CppConvertOperation>(value.operation)
        && source.category == SemanticValueCategory::Place) {
        return expression(value.operands.front().expression, destination);
    }

    auto sources = std::vector<Operand>();
    for (const auto& argument : value.operands) {
        const auto receiver = sources.empty()
            && (std::holds_alternative<CppMemberOperation>(value.operation)
                || std::holds_alternative<CppIndexOperation>(value.operation));
        sources.push_back(
            {argument.expression,
             receiver ? (argument.access == AccessMode::Write ? OperandUse::Place
                                                              : OperandUse::ConstPlace)
                 : argument.access == AccessMode::Write ? OperandUse::Place
                 : argument.access == AccessMode::Take  ? OperandUse::Own
                                                        : OperandUse::Read}
        );
    }
    auto arguments = operands(sources, destination);
    if (!arguments) {
        return std::nullopt;
    }
    if (const auto* operation = std::get_if<CppBinaryOperation>(&value.operation)) {
        return binary(
            std::move((*arguments)[0]),
            operation->operation,
            std::move((*arguments)[1]),
            source.type.resolved()
        );
    }
    if (const auto* update = std::get_if<CppUpdateOperation>(&value.operation)) {
        return prefix_expression(
            update->increment ? TargetPrefixOperator::Increment : TargetPrefixOperator::Decrement,
            std::move(arguments->front())
        );
    }
    if (const auto* operation = std::get_if<CppUnaryOperation>(&value.operation)) {
        const auto prefix = operation->operation == UnaryOperator::LogicalNot
            ? TargetPrefixOperator::LogicalNot
            : operation->operation == UnaryOperator::Negate ? TargetPrefixOperator::Negate
                                                            : TargetPrefixOperator::BitwiseNot;
        return prefix_expression(prefix, std::move((*arguments)[0]));
    }
    if (const auto* member = std::get_if<CppMemberOperation>(&value.operation)) {
        return member_expression(
            std::move(arguments->front()),
            TargetIdentifier::from_spelling(member->name)
        );
    }
    if (std::holds_alternative<CppIndexOperation>(value.operation)) {
        return TargetExpr {
            .value = TargetIndexExpr {
                .operand = UniqueIndirect(std::move((*arguments)[0])),
                .index = UniqueIndirect(std::move((*arguments)[1]))
            }
        };
    }
    if (const auto* conversion = std::get_if<CppConvertOperation>(&value.operation);
        conversion != nullptr && conversion->explicit_cast) {
        return TargetExpr {
            .value = TargetStaticCastExpr {
                .type = context.lower_type(source.type.resolved()),
                .operand = UniqueIndirect(std::move(arguments->front()))
            }
        };
    }
    return TargetExpr {
        .value = TargetConstructionExpr {
            .type = context.lower_type(source.type.resolved()),
            .initializer = std::move(*arguments)
        }
    };
}

} // namespace body_lowering
