module carven:backend.construction.stmt.impl;

import :backend.construction;
import :backend.construction.builder;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto BodyConstructionBuilder::statement(const SemanticStatement& source) noexcept
    -> ConstructionStatement {
    const auto value = std::visit(
        Overloaded {
            [&](const SemReturn& item) noexcept -> ConstructionStatementValue {
                auto result = std::optional<ConstructionExpressionID>();
                if (item.value) {
                    result = expression(*item.value);
                }
                return ConstructionReturn {.value = result};
            },
            [&](const SemBreak&) noexcept -> ConstructionStatementValue {
                if (!loop) {
                    invariant_violation("construction break has no enclosing loop");
                }
                return ConstructionLoopTransfer {.loop = *loop, .continue_loop = false};
            },
            [&](const SemContinue&) noexcept -> ConstructionStatementValue {
                if (!loop) {
                    invariant_violation("construction continue has no enclosing loop");
                }
                return ConstructionLoopTransfer {.loop = *loop, .continue_loop = true};
            },
            [&](const SemThrow& item) noexcept -> ConstructionStatementValue {
                return ConstructionThrow {
                    .value = expression(item.value),
                    .destination = failure_exit
                };
            },
            [&](const SemRethrow&) noexcept -> ConstructionStatementValue {
                if (!caught) {
                    invariant_violation("construction rethrow has no caught failure");
                }
                return ConstructionRethrow {.source = *caught, .destination = failure_exit};
            },
            [&](const SemExpressionStatement& item) noexcept -> ConstructionStatementValue {
                return ConstructionDiscard {.expression = expression(item.expression)};
            },
            [&](const SemInitialize& item) noexcept -> ConstructionStatementValue {
                return ConstructionInitialize {
                    .binding = item.binding,
                    .initializer = expression(item.initializer)
                };
            },
            [&](const SemAssign& item) noexcept -> ConstructionStatementValue {
                return ConstructionAssign {
                    .target = expression(item.target),
                    .compound = item.compound,
                    .value = expression(item.value)
                };
            },
            [&](const OwnedSemanticRegion& item) noexcept -> ConstructionStatementValue {
                return ConstructionScope {.region = region(*item)};
            },
            [&](const SemLoop& item) noexcept -> ConstructionStatementValue {
                const auto identity = reserve_region();
                const auto initializer = region(*item.initializer);
                auto condition = std::optional<ConstructionExpressionID>();
                if (item.condition) {
                    condition = expression(*item.condition);
                }
                const auto outer = loop;
                loop = identity;
                const auto contents = region(*item.body);
                loop = outer;
                const auto steps = region(*item.steps);
                auto statements = std::vector<ConstructionStatement>();
                statements.push_back(
                    {.lifetime = source.lifetime,
                     .origin = source.origin,
                     .value = ConstructionLoop {
                         .initializer = initializer,
                         .condition = condition,
                         .body = contents,
                         .steps = steps
                     }}
                );
                regions[identity.index()] = ConstructionRegion {
                    .lifetime = source.lifetime,
                    .origin = source.origin,
                    .statements = std::move(statements),
                    .result = std::nullopt
                };
                return ConstructionScope {.region = identity};
            },
            [&](const SemRangeLoop& item) noexcept -> ConstructionStatementValue {
                const auto identity = reserve_region();
                const auto range = std::visit(
                    Overloaded {
                        [&](const SemIntegerRange& input) noexcept
                            -> decltype(ConstructionRangeLoop::source) {
                            return ConstructionIntegerRange {
                                .begin = expression(input.begin),
                                .end = expression(input.end)
                            };
                        },
                        [&](const SemSequenceRange& input) noexcept
                            -> decltype(ConstructionRangeLoop::source) {
                            return ConstructionSequenceRange {.value = expression(input.value)};
                        }
                    },
                    item.source
                );
                const auto outer = loop;
                loop = identity;
                const auto contents = region(*item.body);
                loop = outer;
                auto statements = std::vector<ConstructionStatement>();
                statements.push_back(
                    {.lifetime = source.lifetime,
                     .origin = source.origin,
                     .value = ConstructionRangeLoop {
                         .lifetime = item.lifetime,
                         .access = item.access,
                         .binding = item.binding,
                         .source = range,
                         .body = contents
                     }}
                );
                regions[identity.index()] = ConstructionRegion {
                    .lifetime = source.lifetime,
                    .origin = source.origin,
                    .statements = std::move(statements),
                    .result = std::nullopt
                };
                return ConstructionScope {.region = identity};
            },
            [&](const SemTestReport& item) noexcept -> ConstructionStatementValue {
                auto condition = std::optional<ConstructionExpressionID>();
                auto message = std::optional<ConstructionExpressionID>();
                if (item.condition) {
                    condition = expression(*item.condition);
                }
                if (item.message) {
                    message = expression(*item.message);
                }
                return ConstructionTestReport {
                    .kind = item.kind,
                    .condition = condition,
                    .message = message,
                    .condition_source = item.condition_source
                };
            }
        },
        source.value
    );
    return {.lifetime = source.lifetime, .origin = source.origin, .value = value};
}
