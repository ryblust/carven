module carven:backend.preparation.body.impl;

import :backend.preparation.body;
import :semantic.semir.children;
import :semantic.semir.evaluation;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.invariant;
import std;

namespace {

// Read scalar parameters own immutable copies. Owners and Take parameters can
// still be exposed as native T&&; captures can change with their enclosing closure.
auto stable_binding(const SemIRProgram& semantic, const LocalBinding& binding) noexcept -> bool {
    const auto* parameter = std::get_if<ParameterBindingStorage>(&binding.storage);
    if (parameter == nullptr || parameter->access != AccessMode::Read) {
        return false;
    }
    const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic.types().type(binding.type).value);
    return builtin != nullptr
        && (builtin_is_numeric(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char);
}

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
        const auto rule = evaluation_rule(semantic, source);
        auto execution = rule.action == EvaluationAction::Required;
        const auto* binding = std::get_if<SemBinding>(&source.value);
        auto reads = execution
            || (binding != nullptr
                && !stable_binding(semantic, metadata.binding(binding->binding)));
        for (const auto* input : rule.operands) {
            if (input != nullptr) {
                const auto& child = summary(*input);
                execution |= child.requires_execution;
                reads |= child.reads_storage;
            }
        }
        auto conditional = std::holds_alternative<SemIf>(source.value)
            || std::holds_alternative<SemMatch>(source.value)
            || std::holds_alternative<SemTry>(source.value)
            || rule.action == EvaluationAction::ShortCircuit
            || std::holds_alternative<SemReport>(source.value);
        // Required operations may omit executed children from their evaluation
        // rule. Storage scope includes their nested regions; a short circuit
        // with a known left value includes only its selected operands.
        if (std::holds_alternative<SemShortCircuit>(source.value)) {
            for (const auto* input : rule.operands) {
                if (input != nullptr) {
                    conditional |= summary(*input).conditional_evaluation;
                }
            }
        } else {
            visit_semantic_children(source.value, [&](const auto& child) noexcept {
                if constexpr (std::same_as<
                                  std::remove_cvref_t<decltype(child)>,
                                  SemanticExpression>) {
                    conditional |= summary(child).conditional_evaluation;
                }
            });
        }
        summaries.emplace(
            std::addressof(source),
            ExpressionSummary {
                .requires_execution = execution,
                .reads_storage = reads,
                .conditional_evaluation = conditional
            }
        );
    };

    const auto visitor = PreparationVisitor {.operation = prepare};
    visit_semantic_nodes(metadata.region(), visitor);
}

auto BodyPreparation::body() const noexcept -> const SemIRBody& {
    return metadata;
}

auto BodyPreparation::operation(const SemanticExpression& source) noexcept
    -> const SemanticExpression& {
    auto* selected = std::addressof(source);
    while (const auto* propagation = std::get_if<SemPropagate>(&selected->value)) {
        selected = std::addressof(*propagation->operand);
    }
    return *selected;
}

auto BodyPreparation::summary(const SemanticExpression& source) const noexcept
    -> const ExpressionSummary& {
    return summaries.at(std::addressof(operation(source)));
}

auto BodyPreparation::prepare(const SemanticExpression& input) const noexcept -> PreparedOperation {
    const auto& source = operation(input);
    auto preparation = prepare_operation(semantic, source);
    auto inputs = operands(source, preparation.get());
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
    if (const auto* construction = std::get_if<PreparedNativeConstruction>(preparation.get())) {
        for (const auto [index, argument] : std::views::enumerate(construction->arguments)) {
            if (std::holds_alternative<const CppConstructArgument*>(argument)) {
                inputs[index].demand = PreparedDemand::Effects;
            }
        }
    }
    const auto& effect = summary(source);
    return PreparedOperation {
        .operation = source,
        .executes_operation =
            evaluation_rule(semantic, source).action == EvaluationAction::Required,
        .requires_execution = effect.requires_execution,
        .reads_storage = effect.reads_storage,
        .operands = std::move(inputs),
        .preparation = std::move(preparation)
    };
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
