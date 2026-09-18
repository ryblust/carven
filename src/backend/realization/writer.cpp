module carven:backend.realization.writer.impl;

import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.expr;
import :backend.realization.format;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir.format;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

auto BodyRealizer::ExpressionBuilder::complete_writer(
    Fragment& fragment,
    const SemFormat& format,
    const PreparedWriterFormat& preparation,
    std::span<Fragment> children,
    std::span<const PreparedOperand> inputs,
    std::optional<TargetLocalID> output
) noexcept -> void {
    const auto offset = format.receiver ? 1uz : 0uz;
    // Existing conflict barriers have already captured reads before later effects.
    // Complete remaining nontrivial inputs before any reservation or write.
    for (auto index = 0uz; index < inputs.size(); ++index) {
        if (inputs[index].demand != PreparedDemand::Value) {
            continue;
        }
        auto& child = children[index];
        if (!std::holds_alternative<LocalBindingID>(child.completion)) {
            anchor(
                child,
                inputs[index].use,
                !std::holds_alternative<ConstantID>(child.completion),
                true
            );
            adopt(child);
        }
    }
    auto operands = std::vector<TargetExpr>();
    auto sizes = std::vector<TargetExpr>();
    for (auto index = 0uz; index < preparation.operand_indices.size(); ++index) {
        auto& child = children[preparation.operand_indices[index] + offset];
        operands.push_back(raw(child));
    }
    auto operand = 0uz;
    for (const auto& field : preparation.format.fields) {
        auto& child = children[preparation.operand_indices[operand] + offset];
        operand += writer_field_operand_count(field);
        const auto* type = std::get_if<BuiltinType>(&field);
        if (type != nullptr && (*type == BuiltinType::Str || *type == BuiltinType::String)) {
            sizes.push_back(call_member(raw(child), "size", {}));
        }
    }
    if (output) {
        const auto type = owner.context.lower_type(source(fragment).operation.type.resolved());
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = *output,
                .type = type,
                .initializer =
                    TargetExpr {.value = TargetConstructionExpr {.type = type, .initializer = {}}}
            }
        ));
    }
    auto writes = realize_writer_statements(
        owner.context,
        preparation.format,
        owner.fresh_local(TargetTemporaryNameKind::Operand),
        output ? name_expression(*output) : emit(children.front(), PreparedUse::WritePlace),
        std::move(operands),
        std::move(sizes)
    );
    for (auto& statement : writes) {
        statements.emit(std::move(statement));
    }
    if (output) {
        complete(fragment, name_expression(*output));
    } else {
        complete(fragment, LoweringCompleted {});
    }
}
