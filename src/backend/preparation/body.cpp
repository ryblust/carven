module carven:backend.preparation.body.impl;

import :backend.preparation.body;
import :semantic.semir.evaluation;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.invariant;
import std;

namespace {

template<typename Operation>
struct PreparationVisitor final {
    const Operation& operation;

    auto leave(const SemanticExpression& source) const noexcept -> void { operation(source); }
};

} // namespace

BodyPreparation::BodyPreparation(const SemIRProgram& semantic, BodyID body) noexcept
    : semantic(semantic),
      metadata(semantic.bodies().body(body)) {
    const auto prepare = [&](const SemanticExpression& source) noexcept {
        if (std::holds_alternative<SemPropagate>(source.value)) {
            return;
        }
        auto inputs = operands(source);
        auto preparation = prepare_operation(semantic, source);
        if (const auto* prepared = std::get_if<PreparedFormat>(preparation.get())) {
            const auto& format = std::get<SemFormat>(source.value);
            const auto offset = format.receiver ? 1uz : 0uz;
            for (auto index = offset; index < inputs.size(); ++index) {
                inputs[index].demand = PreparedDemand::Effects;
            }
            for (const auto index : prepared_format_operands(*prepared)) {
                inputs.at(index + offset).demand = PreparedDemand::Value;
            }
        } else if (const auto* prepared = std::get_if<PreparedPrint>(preparation.get())) {
            for (auto index = 0uz; index < inputs.size(); ++index) {
                if (prepared->operand_text[index]) {
                    inputs[index].demand = PreparedDemand::Effects;
                }
            }
        }
        const auto rule = evaluation_rule(semantic, source);
        auto execution = rule.action == EvaluationAction::Required;
        auto reads = execution || std::holds_alternative<SemBinding>(source.value);
        for (const auto* input : rule.operands) {
            if (input != nullptr) {
                const auto& child = operation(*input);
                execution |= child.requires_execution;
                reads |= child.reads_storage;
            }
        }
        operations.emplace(
            std::addressof(source),
            PreparedOperation {
                .operation = source,
                .executes_operation = rule.action == EvaluationAction::Required,
                .requires_execution = execution,
                .reads_storage = reads,
                .operands = std::move(inputs),
                .preparation = std::move(preparation)
            }
        );
    };

    const auto visitor = PreparationVisitor {.operation = prepare};
    visit_semantic_nodes(metadata.region(), visitor);
}

auto BodyPreparation::body() const noexcept -> const SemIRBody& {
    return metadata;
}

auto BodyPreparation::operation(const SemanticExpression& source) const noexcept
    -> const PreparedOperation& {
    auto* selected = std::addressof(source);
    while (const auto* propagation = std::get_if<SemPropagate>(&selected->value)) {
        selected = std::addressof(*propagation->operand);
    }
    const auto found = operations.find(selected);
    if (found == operations.end()) {
        invariant_violation("prepared operation does not belong to this body");
    }
    return found->second;
}

auto BodyPreparation::operand(const SemanticExpression& source, PreparedUse use) const noexcept
    -> PreparedOperand {
    return {.expression = std::addressof(source), .use = use, .demand = PreparedDemand::Value};
}

auto BodyPreparation::argument(const SemCallArgument& source) const noexcept -> PreparedOperand {
    auto use = PreparedUse::ReadBorrow;
    if (source.access == AccessMode::Write) {
        use = PreparedUse::WritePlace;
    } else if (source.access == AccessMode::Take) {
        use = PreparedUse::Consume;
    } else if (std::holds_alternative<PointerTypeValue>(
                   semantic.types().type(source.expression.type.resolved()).value
               )) {
        use = PreparedUse::AddressValue;
    }
    return operand(source.expression, use);
}
