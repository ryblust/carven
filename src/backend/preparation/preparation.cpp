module carven:backend.preparation.impl;

import :backend.preparation;
import :semantic.format.builtin;
import :semantic.semir.constant_access;
import :support.invariant;
import std;

auto prepare_operation(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> std::unique_ptr<OperationPreparation> {
    if (const auto* unary = std::get_if<SemUnary>(&source.value)) {
        return std::make_unique<OperationPreparation>(
            prepare_unary(program, unary->operation, source.type.resolved())
        );
    }
    if (const auto* binary = std::get_if<SemBinary>(&source.value)) {
        return std::make_unique<OperationPreparation>(prepare_binary(
            program,
            binary->operation,
            source.type.resolved(),
            binary->right->constant
        ));
    }
    if (const auto* native = std::get_if<SemCpp>(&source.value)) {
        if (std::holds_alternative<CppConstructOperation>(native->operation)) {
            const auto* type =
                std::get_if<CppTypeValue>(&program.types().type(source.type.resolved()).value);
            const auto* query = type == nullptr ? nullptr : std::get_if<CppQueryType>(&type->form);
            const auto* construction =
                query == nullptr ? nullptr : std::get_if<CppConstructQuery>(&query->expression);
            if (construction == nullptr
                || construction->arguments.size() != native->operands.size()) {
                invariant_violation("native construction requires its matching argument query");
            }
            auto plan = PreparedNativeConstruction {};
            auto retained = 0uz;
            for (const auto& argument : construction->arguments) {
                if (argument.constant) {
                    plan.arguments.emplace_back(std::addressof(argument));
                } else {
                    plan.arguments.emplace_back(retained++);
                }
            }
            return std::make_unique<OperationPreparation>(std::move(plan));
        }
        if (const auto* unary = std::get_if<CppUnaryOperation>(&native->operation)) {
            return std::make_unique<OperationPreparation>(
                prepare_unary(program, unary->operation, source.type.resolved())
            );
        }
        if (const auto* binary = std::get_if<CppBinaryOperation>(&native->operation)) {
            return std::make_unique<OperationPreparation>(
                prepare_binary(program, binary->operation, source.type.resolved())
            );
        }
    }
    const auto* callable = std::get_if<SemBorrowCallable>(&source.value);
    const auto* array = std::get_if<SemArrayAdopt>(&source.value);
    if (callable != nullptr || array != nullptr) {
        const auto& input = callable != nullptr ? *callable->source : *array->source;
        return std::make_unique<OperationPreparation>(PreparedCallableAdaptation {
            .adaptation =
                callable_adaptation(program, input.type.resolved(), source.type.resolved()),
            .array = array != nullptr
        });
    }
    const auto values = PublishedConstantValues(program);
    if (const auto* format = std::get_if<SemFormat>(&source.value)) {
        auto types = std::vector<std::optional<BuiltinType>>();
        auto known = std::vector<std::optional<ConstantID>>();
        for (const auto& operand : format->operands) {
            const auto& type = program.types().type(operand.expression.type.resolved());
            const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
            types.push_back(builtin ? std::optional(builtin->kind) : std::nullopt);
            known.push_back(operand.expression.constant);
        }
        return std::make_unique<OperationPreparation>(
            prepare_format(values, format->specification, types, known)
        );
    }
    if (const auto* print = std::get_if<SemPrint>(&source.value)) {
        auto prepared = PreparedPrint {};
        for (auto index = 0uz; index < print->operands.size(); ++index) {
            if (const auto known = print->operands[index].expression.constant) {
                const auto& fact = values.constant(*known);
                if (std::holds_alternative<IntegerConstant>(fact.value)
                    || std::holds_alternative<F32Constant>(fact.value)
                    || std::holds_alternative<F64Constant>(fact.value)
                    || std::holds_alternative<BooleanConstant>(fact.value)
                    || std::holds_alternative<CharacterConstant>(fact.value)) {
                    if (auto formatted = format_builtin_value(
                            builtin_format_value(values, fact),
                            {},
                            maximum_prepared_format_bytes
                        )) {
                        if (prepared.operand_text.empty()) {
                            prepared.operand_text.resize(print->operands.size());
                        }
                        prepared.operand_text[index] = std::move(*formatted);
                    }
                }
            }
        }
        if (prepared.operand_text.empty()) {
            return nullptr;
        }
        return std::make_unique<OperationPreparation>(std::move(prepared));
    }
    return nullptr;
}
