module carven:backend.construction.expr.impl;

import :backend.construction.builder;
import :backend.construction;
import :semantic.semir.body;
import :semantic.semir.evaluation;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyConstructionBuilder::expression(const SemanticExpression& source) noexcept
    -> ConstructionExpressionID {
    if (const auto* propagation = std::get_if<SemPropagate>(&source.value)) {
        const auto result = expression(*propagation->operand);
        source_expressions.emplace(std::addressof(source), result);
        return result;
    }
    if (expressions.size() >= std::numeric_limits<std::uint32_t>::max()) {
        invariant_violation("construction expression limit exceeded");
    }
    const auto id =
        ConstructionExpressionID(body.id(), static_cast<std::uint32_t>(expressions.size()));
    expressions.emplace_back();
    auto value = source.value.visit(
        Overloaded {
            [&](const SemTestReport& item) noexcept -> ConstructionExpressionValue {
                auto condition = std::optional<ConstructionExpressionID>();
                auto message = std::optional<ConstructionExpressionID>();
                if (item.condition) {
                    condition = expression(**item.condition);
                }
                if (item.message) {
                    message = expression(**item.message);
                }
                return ConstructionTestReport {
                    .kind = item.kind,
                    .condition = condition,
                    .message = message,
                    .condition_source = item.condition_source
                };
            },
            [&](const auto&) noexcept -> ConstructionExpressionValue {
                auto inputs = operands(source);
                auto failure = std::optional<ConstructionFallible>();
                if (const auto* call = std::get_if<SemCall>(&source.value); call != nullptr
                    && (!semantic.failure_sets()
                             .failure_set(call->callee_failures.resolved())
                             .members.empty()
                        || semantic.may_stop_test(call->callee->type.resolved()))) {
                    failure = ConstructionFallible {
                        .failures = call->callee_failures.resolved(),
                        .destination = failure_exit
                    };
                }
                auto preparation = prepare_operation(semantic, source);
                if (const auto* prepared = std::get_if<PreparedFormat>(preparation.get())) {
                    const auto* format = std::get_if<SemFormat>(&source.value);
                    const auto offset = format->receiver ? 1uz : 0uz;
                    for (auto index = offset; index < inputs.size(); ++index) {
                        inputs[index].demand = ConstructionDemand::Effects;
                    }
                    for (const auto index : prepared_format_operands(*prepared)) {
                        inputs.at(index + offset).demand = ConstructionDemand::Value;
                    }
                } else if (const auto* prepared = std::get_if<PreparedPrint>(preparation.get())) {
                    for (auto index = 0uz; index < inputs.size(); ++index) {
                        if (prepared->operand_text[index]) {
                            inputs[index].demand = ConstructionDemand::Effects;
                        }
                    }
                }
                return ConstructionOperation {
                    .operands = std::move(inputs),
                    .failure = failure,
                    .preparation = std::move(preparation)
                };
            },
            [&](const SemShortCircuit& value) noexcept -> ConstructionExpressionValue {
                const auto condition = expression(*value.left);
                const auto selected = expression(*value.right);
                return ConstructionShortCircuit {
                    .condition = condition,
                    .operation = value.operation,
                    .selected = selected
                };
            },
            [&](const SemIf& value) noexcept -> ConstructionExpressionValue {
                auto branches = std::vector<ConstructionConditionalBranch>();
                for (const auto& branch : value.branches) {
                    const auto condition = expression(branch.condition);
                    const auto body = region(branch.body);
                    branches.push_back({.condition = condition, .body = body});
                }
                auto otherwise = std::optional<ConstructionRegionID>();
                if (value.otherwise) {
                    otherwise = region(**value.otherwise);
                }
                return ConstructionConditional {
                    .branches = std::move(branches),
                    .otherwise = otherwise
                };
            },
            [&](const SemMatch& value) noexcept -> ConstructionExpressionValue {
                const auto subject = expression(*value.subject);
                auto arms = std::vector<ConstructionMatchArm>();
                for (const auto& arm : value.arms) {
                    if (!arm.reachable) {
                        continue;
                    }
                    auto pattern_bounds = std::vector<ConstructionPatternBounds>();
                    for (const auto& range : arm.pattern_bounds) {
                        pattern_bounds.push_back(
                            {.pattern = range.pattern,
                             .begin = range.begin ? std::optional(expression(*range.begin))
                                                  : std::nullopt,
                             .end =
                                 range.end ? std::optional(expression(*range.end)) : std::nullopt}
                        );
                    }
                    auto guard = std::optional<ConstructionExpressionID>();
                    if (arm.guard) {
                        guard = expression(*arm.guard);
                    }
                    arms.push_back(
                        {.pattern_id = arm.pattern,
                         .bindings = arm.bindings,
                         .guard = guard,
                         .body = region(arm.body),
                         .pattern_bounds = std::move(pattern_bounds)}
                    );
                }
                return ConstructionMatch {
                    .subject = subject,
                    .subject_is_place = value.subject_is_place,
                    .arms = std::move(arms)
                };
            },
            [&](const SemTry& value) noexcept -> ConstructionExpressionValue {
                const auto outer = failure_exit;
                failure_exit = ConstructionHandlerExit {.handler = id};
                const auto protected_body = region(*value.body);
                failure_exit = outer;
                auto arms = std::vector<ConstructionCatchArm>();
                for (const auto& arm : value.arms) {
                    if (semantic.failure_sets()
                            .failure_set(arm.accepted_failures.resolved())
                            .members.empty()) {
                        continue;
                    }
                    auto alternatives = std::vector<ConstructionCatchAlternative>();
                    for (const auto& alternative : arm.alternatives) {
                        if (!alternative.reachable) {
                            continue;
                        }
                        alternatives.push_back(
                            {.pattern = alternative.pattern.visit(
                                 Overloaded {
                                     [](CatchAllPattern value) static noexcept
                                         -> decltype(ConstructionCatchAlternative::pattern) {
                                         return value;
                                     },
                                     [&](const SemTypedCatchPattern& value) noexcept
                                         -> decltype(ConstructionCatchAlternative::pattern) {
                                         return ConstructionTypedCatch {
                                             .type = value.type.resolved(),
                                             .pattern_id = value.inner
                                         };
                                     }
                                 }
                             )}
                        );
                    }
                    const auto previous = caught;
                    caught = ConstructionCaughtFailure {
                        .handler = id,
                        .failures = arm.accepted_failures.resolved()
                    };
                    auto pattern_bounds = std::vector<ConstructionPatternBounds>();
                    for (const auto& range : arm.pattern_bounds) {
                        pattern_bounds.push_back(
                            {.pattern = range.pattern,
                             .begin = range.begin ? std::optional(expression(*range.begin))
                                                  : std::nullopt,
                             .end =
                                 range.end ? std::optional(expression(*range.end)) : std::nullopt}
                        );
                    }
                    auto guard = std::optional<ConstructionExpressionID>();
                    if (arm.guard) {
                        guard = expression(*arm.guard);
                    }
                    const auto handler_body = region(arm.body);
                    caught = previous;
                    arms.push_back(
                        {.accepted_failures = arm.accepted_failures.resolved(),
                         .alternatives = std::move(alternatives),
                         .bindings = arm.bindings,
                         .guard = guard,
                         .body = handler_body,
                         .pattern_bounds = std::move(pattern_bounds)}
                    );
                }
                return ConstructionTry {
                    .body = protected_body,
                    .protected_failures = value.protected_failures.resolved(),
                    .residual_failures = value.residual_failures.resolved(),
                    .residual_destination = outer,
                    .arms = std::move(arms)
                };
            },
            [](const SemPropagate&) static noexcept -> ConstructionExpressionValue {
                invariant_violation("construction propagation was not forwarded");
            }
        }
    );
    const auto rule = evaluation_rule(semantic, source);
    auto execution = rule.action == EvaluationAction::Required;
    auto reads = execution || std::holds_alternative<SemBinding>(source.value);
    for (const auto* input : rule.operands) {
        if (input == nullptr) {
            continue;
        }
        const auto& child = *expressions[source_expressions.at(input).index()];
        execution |= child.requires_execution;
        reads |= child.reads_storage;
    }
    expressions[id.index()].emplace(
        ConstructionExpression {
            .operation = source,
            .type = source.type.resolved(),
            .lifetime = source.lifetime,
            .origin = source.origin,
            .category = source.category,
            .constant = source.constant,
            .executes_operation = rule.action == EvaluationAction::Required,
            .requires_execution = execution,
            .reads_storage = reads,
            .exits_test = semantic.may_stop_test(source),
            .failures = source.failures.resolved(),
            .value = std::move(value)
        }
    );
    source_expressions.emplace(std::addressof(source), id);
    return id;
}
