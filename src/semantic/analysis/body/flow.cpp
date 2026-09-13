module carven:semantic.analysis.body.flow.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::active_builder() noexcept -> BodyBuilder& {
    body_builder.set_lifetime(active_full_expression.value_or(frames.back().lifetime));
    return body_builder;
}

auto BodyElaborator::begin_full_expression(Span span) noexcept -> void {
    if (active_full_expression.has_value()) {
        invariant_violation("nested full expression");
    }
    active_full_expression = body_builder.add_lifetime_region(
        frames.back().lifetime,
        LifetimeRegionKind::FullExpression,
        origin(span)
    );
}

auto BodyElaborator::end_full_expression(Span) noexcept -> void {
    active_full_expression.reset();
}

auto BodyElaborator::empty_region(Span span) noexcept -> SemanticRegion {
    return {
        .lifetime = frames.back().lifetime,
        .origin = origin(span),
        .statements = {},
        .result = std::nullopt,
        .failures = BodyFailures(draft().add_empty_failure_term()),
        .exits_test = false
    };
}

auto BodyElaborator::take_built(BuiltExpression& expression, Span span) noexcept
    -> SemanticExpression {
    auto& built = expression;
    auto result = std::visit(
        Overloaded {
            [&](SemanticExpression& value) noexcept { return std::move(value); },
            [&](PlaceExpression& place) noexcept {
                auto result = std::move(place.expression);
                result.origin = origin(span);
                return result;
            },

        },
        built.storage
    );
    return result;
}

auto BodyElaborator::make_built(
    ConstructionTypeRef type,
    SemanticExpressionValue value,
    Span span,
    BodyPendingFailureTerms pending,
    std::optional<ConstantID> constant
) noexcept -> BuiltExpression {
    auto expression = active_builder().make_expression(
        type,
        active_builder().lifetime(),
        origin(span),
        std::move(value),
        constant
    );
    for (const auto term : pending) {
        draft().add_failure_contribution(expression.failures.term(), term);
    }
    return BuiltExpression {
        .storage = std::move(expression),

        .pending_failures = std::move(pending)
    };
}

auto BodyElaborator::mark_noncompleting(BuiltExpression value) noexcept -> BuiltExpression {
    value.completes = false;
    return value;
}

auto BodyElaborator::append_statement(
    decltype(SemanticStatement::value) value,
    ProgramOriginID statement_origin
) noexcept -> void {
    auto& destination = regions.back();
    const auto statement_failures = draft().add_empty_failure_term();
    auto statement_exits_test = false;
    const auto add = [&](const SemanticExpression& child) noexcept {
        draft().add_failure_contribution(statement_failures, child.failures.term());
        statement_exits_test |= child.exits_test;
    };
    const auto add_region = [&](const SemanticRegion& child) noexcept {
        draft().add_failure_contribution(statement_failures, child.failures.term());
        statement_exits_test |= child.exits_test;
        if (child.result.has_value()) {
            add(*child.result);
        }
    };
    std::visit(
        Overloaded {
            [&](const SemReturn& node) noexcept {
                if (node.value.has_value()) {
                    add(*node.value);
                }
            },
            [](const SemBreak&) static noexcept {},
            [](const SemContinue&) static noexcept {},
            [&](const SemRethrow&) noexcept {
                if (catches.empty()) {
                    invariant_violation("rethrow statement has no catch context");
                }
                draft().add_failure_contribution(statement_failures, catches.back().failures);
            },
            [&](const SemThrow& node) noexcept {
                add(node.value);
                draft().add_failure_member(statement_failures, node.failure_type);
            },
            [&](const SemExpressionStatement& node) noexcept { add(node.expression); },
            [&](const SemInitialize& node) noexcept { add(node.initializer); },
            [&](const SemAssign& node) noexcept {
                add(node.target);
                add(node.value);
            },
            [&](const SemLoop& node) noexcept {
                add_region(*node.initializer);
                if (node.condition.has_value()) {
                    add(*node.condition);
                    const auto known = known_boolean_constant(draft(), node.condition->constant);
                    if (known.has_value() && !*known) {
                        return;
                    }
                }
                add_region(*node.body);
                add_region(*node.steps);
            },
            [&](const SemRangeLoop& node) noexcept {
                std::visit(
                    [&](const auto& range) noexcept {
                        if constexpr (std::same_as<
                                          std::remove_cvref_t<decltype(range)>,
                                          SemIntegerRange>) {
                            add(range.begin);
                            add(range.end);
                        } else {
                            add(range.value);
                        }
                    },
                    node.source
                );
                add_region(*node.body);
            },
            [&](const OwnedSemanticRegion& node) noexcept { add_region(*node); },
        },
        value
    );
    destination.failures = BodyFailures(
        draft().add_union_failure_term({destination.failures.term(), statement_failures})
    );
    destination.exits_test |= statement_exits_test;
    destination.statements.push_back(
        {.origin = statement_origin,
         .lifetime = active_full_expression.value_or(frames.back().lifetime),
         .value = std::move(value)}
    );
}

auto BodyElaborator::append_expression(BuiltExpression& expression, Span span) noexcept -> void {
    append_statement(SemExpressionStatement {take_built(expression, span)}, origin(span));
}

auto BodyElaborator::inferred_result_type() const noexcept -> ConstructionTypeRef {
    if (!result_type.has_value()) {
        invariant_violation("body producer result was queried before body completion");
    }
    return *result_type;
}

auto BodyElaborator::ensure_reachable_diagnostics(Span span) noexcept -> void {
    if (!reachable && !reported_unreachable) {
        warn(span, DiagnosticCode::FlowUnreachable, "statement is unreachable");
        reported_unreachable = true;
    }
}
