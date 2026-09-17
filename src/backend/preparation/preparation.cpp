module carven:backend.preparation.impl;

import :backend.preparation;
import :semantic.format.builtin;
import :semantic.semir.constant_access;
import std;

auto prepare_operation(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> std::unique_ptr<OperationPreparation> {
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
