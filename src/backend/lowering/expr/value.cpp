module carven:backend.lowering.expr.value.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct OrderedAggregateValues final {
    std::vector<TargetStmtID> prelude;
    std::vector<TargetExprID> values;
};

auto lower_ordered_aggregate_values(
    TargetCallableLowerer& context,
    std::span<const HIRExprID> expressions,
    const TargetControlDestinations& control,
    std::span<const std::uint32_t> target_order = {}
) noexcept -> OrderedAggregateValues {
    auto lowered = std::vector<LoweredExpression>();
    lowered.reserve(expressions.size());
    for (const auto expression : expressions) {
        lowered.push_back(lower_expression(context, expression, control));
    }

    auto result = OrderedAggregateValues();
    result.values.reserve(lowered.size());
    for (auto index = 0uz; index < lowered.size(); ++index) {
        auto& value = lowered[index];
        auto requires_snapshot = false;
        for (auto later = index + 1; later < lowered.size(); ++later) {
            const auto reordered =
                !target_order.empty() && target_order[later] < target_order[index];
            if ((reordered || !lowered[later].prelude.empty())
                && !TargetEvaluationSequencer::expressions_commute(
                    context,
                    expressions[index],
                    expressions[later]
                )) {
                requires_snapshot = true;
                break;
            }
        }
        if (requires_snapshot) {
            value = TargetEvaluationSequencer::materialize(
                context,
                std::move(value),
                MaterializationKind::Snapshot,
                TargetMaterializationReason::EvaluationOrder
            );
        }
        result.prelude.insert(
            result.prelude.end(),
            std::make_move_iterator(value.prelude.begin()),
            std::make_move_iterator(value.prelude.end())
        );
        result.values.push_back(value.expression);
    }
    return result;
}

} // namespace

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRLiteralExpr& expression,
    const TargetControlDestinations&
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    return {
        .prelude = {},
        .expression = context.target().append_expression({
            .value = TargetLiteralExpr {
                .value = lower_literal(context, expression.value, source.type),
            },
        }),
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRNameExpr& expression,
    const TargetControlDestinations&
) noexcept -> LoweredExpression {
    if (const auto constant = context.source().expression(id).constant) {
        return {.prelude = {}, .expression = lower_constant_value(context, *constant)};
    }
    return {
        .prelude = {},
        .expression = context.target().append_expression({
            .value = TargetNameExpr {
                .name = symbol_reference_name(context, expression.symbol),
            },
        }),
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRArrayExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    const auto& array_type = std::get<HIRArrayTypeValue>(context.source().type(source.type).value);
    auto elements = lower_ordered_aggregate_values(context, expression.element_ids, control);
    const auto extent = context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetIntegerLiteral {
                .negative = false,
                .magnitude = array_type.extent,
                .suffix = TargetIntegerSuffix::None,
            }
        },
    });
    return {
        .prelude = std::move(elements.prelude),
        .expression = context.target().append_expression({
            .value = TargetArrayExpr {
                .element_type_id = lower_type(context, array_type.element_type_id),
                .extent = extent,
                .element_ids = std::move(elements.values),
            },
        }),
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRConstructionExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    const auto* nominal =
        std::get_if<HIRStructTypeValue>(&context.source().type(source.type).value);
    if (nominal == nullptr) {
        invariant_violation("HIR structure construction has a non-structure result type");
    }
    const auto& structure = context.source().structure(nominal->structure);
    const auto enclosing = context.source().provenance().spelling(structure.name);
    auto source_values = expression.fields
        | std::views::transform([](const HIRFieldInitializer& field) static noexcept {
                             return field.value;
                         })
        | std::ranges::to<std::vector>();
    auto target_order = expression.fields
        | std::views::transform([](const HIRFieldInitializer& field) static noexcept {
                            return field.declaration_index;
                        })
        | std::ranges::to<std::vector>();
    auto lowered = lower_ordered_aggregate_values(context, source_values, control, target_order);
    if (lowered.values.size() != expression.fields.size()) {
        invariant_violation("lowered structure construction has a field/value count mismatch");
    }
    auto values = std::vector<std::pair<std::uint32_t, TargetFieldInitializer>>();
    values.reserve(expression.fields.size());
    for (const auto& [field, target_value] : std::views::zip(expression.fields, lowered.values)) {
        const auto declaration_index = static_cast<std::size_t>(field.declaration_index);
        if (declaration_index >= structure.fields.size()) {
            invariant_violation("HIR structure construction field index is out of range");
        }
        values.push_back({
            field.declaration_index,
            TargetFieldInitializer {
                .name = context.name_allocator().source(
                    context.source().provenance().spelling(
                        structure.fields[declaration_index].name
                    ),
                    enclosing
                ),
                .value = target_value,
            },
        });
    }
    std::ranges::sort(values, {}, [](const auto& value) static noexcept { return value.first; });
    auto ordered = std::vector<TargetFieldInitializer>();
    ordered.reserve(values.size());
    for (auto& [index, value] : values) {
        static_cast<void>(index);
        ordered.push_back(std::move(value));
    }
    return {
        .prelude = std::move(lowered.prelude),
        .expression = context.target().append_expression({
            .value = TargetConstructionExpr {
                .type = lower_type(context, source.type),
                .initializer = std::move(ordered),
            },
        }),
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID,
    const HIRCaseConstructionExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto callee = name_expression(
        context,
        symbol_reference_name(context, context.source().enum_case(expression.enum_case).symbol)
    );
    if (expression.payload.empty()) {
        return {.prelude = {}, .expression = callee};
    }
    auto arguments = std::vector<HIRCallArgument>();
    arguments.reserve(expression.payload.size());
    for (const auto value : expression.payload) {
        arguments.push_back({
            .access = HIRAccessMode::Read,
            .expression = value,
        });
    }
    return ordered_call(context, {.prelude = {}, .expression = callee}, arguments, control);
}
