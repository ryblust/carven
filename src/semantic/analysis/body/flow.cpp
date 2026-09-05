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
import :semantic.analysis.constant.proof;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

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

auto BodyElaborator::empty_region(Span span) noexcept -> DraftRegion {
    return {
        .scope = frames.back().scope,
        .lifetime = frames.back().lifetime,
        .origin = origin(span),
        .statements = {},
        .result = std::nullopt,
        .failures = draft().add_empty_failure_term(),
        .exits_test = false
    };
}

auto BodyElaborator::take_built(BuiltExpression& expression, Span span) noexcept
    -> DraftExpression {
    auto& built = expression;
    auto result = std::visit(
        Overloaded {
            [&](ExpressionHandle id) noexcept { return body_builder.take_value(id); },
            [&](PlaceHandle id) noexcept {
                auto result = body_builder.take_place(id);
                result.origin = origin(span);
                return result;
            },
            [&](DirectCallable direct) noexcept {
                return active_builder().callable_expression(direct.callable, origin(span));
            },
            [&](NoExpressionValue) noexcept -> DraftExpression {
                invariant_violation("void expression lost its evaluation");
            }
        },
        built.storage
    );
    result.constant = built.constant;
    return result;
}

auto BodyElaborator::make_built(
    ConstructionTypeRef type,
    DraftExpressionValue value,
    Span span,
    PendingFailureTerms pending
) noexcept -> BuiltExpression {
    auto expression =
        active_builder()
            .make_expression(type, active_builder().lifetime(), origin(span), std::move(value));
    for (const auto term : pending) {
        draft().add_failure_contribution(expression.failures, term);
    }
    const auto id = body_builder.add_expression(std::move(expression));
    return BuiltExpression {
        .type = type,
        .storage = id,
        .constant = std::nullopt,
        .pending_failures = std::move(pending)
    };
}

auto BodyElaborator::mark_noncompleting(BuiltExpression value) noexcept -> BuiltExpression {
    value.completes = false;
    return value;
}

auto BodyElaborator::append_statement(decltype(DraftStatement::value) value, Span span) noexcept
    -> void {
    auto& destination = regions.back();
    const auto statement_failures = draft().add_empty_failure_term();
    auto statement_exits_test = false;
    const auto add = [&](const DraftExpression& child) noexcept {
        draft().add_failure_contribution(statement_failures, child.failures);
        statement_exits_test |= child.exits_test;
    };
    const auto add_region = [&](const DraftRegion& child) noexcept {
        draft().add_failure_contribution(statement_failures, child.failures);
        statement_exits_test |= child.exits_test;
        if (child.result.has_value()) {
            add(*child.result);
        }
    };
    std::visit(
        Overloaded {
            [&](const SemReturn<ConstructionTypeRef, FailureTermID>& node) noexcept {
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
            [&](const SemThrow<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(node.value);
                draft().add_failure_member(statement_failures, node.failure_type);
            },
            [&](const SemExpressionStatement<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(node.expression);
            },
            [&](const SemInitialize<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(node.initializer);
            },
            [&](const SemAssign<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(node.target);
                add(node.value);
            },
            [&](const SemLoop<ConstructionTypeRef, FailureTermID>& node) noexcept {
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
            [&](const SemRangeLoop<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add(node.begin);
                if (node.end.has_value()) {
                    add(*node.end);
                }
                add_region(*node.body);
            },
            [&](const SemTestReport<ConstructionTypeRef, FailureTermID>& node) noexcept {
                if (node.condition.has_value()) {
                    add(*node.condition);
                }
                if (node.message.has_value()) {
                    add(*node.message);
                }
                statement_exits_test |= node.kind == TestReportKind::Fail;
                if (node.kind == TestReportKind::Require) {
                    const auto known = known_boolean_constant(draft(), node.condition->constant);
                    statement_exits_test |= !known.has_value() || !*known;
                }
            },
            [&](const OwnedSemanticRegion<ConstructionTypeRef, FailureTermID>& node) noexcept {
                add_region(*node);
            },
        },
        value
    );
    destination.failures =
        draft().add_union_failure_term({destination.failures, statement_failures});
    destination.exits_test |= statement_exits_test;
    destination.statements.push_back(
        {.origin = origin(span),
         .lifetime = active_full_expression.value_or(frames.back().lifetime),
         .value = std::move(value)}
    );
}

auto BodyElaborator::append_expression(BuiltExpression& expression, Span span) noexcept -> void {
    append_statement(
        SemExpressionStatement<ConstructionTypeRef, FailureTermID> {take_built(expression, span)},
        span
    );
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

auto BodyElaborator::publish_constant(ConstantFact fact, Span span) noexcept -> BuiltExpression {
    const auto type = fact.type;
    const auto constant = draft().intern_constant(std::move(fact));
    const auto value = active_builder().append_value(
        type,
        active_builder().lifetime(),
        SemConstant {.constant = constant},
        origin(span)
    );
    return BuiltExpression {
        .type = type,
        .storage = value,
        .constant = constant,
        .pending_failures = {},
    };
}

} // namespace body_elaboration
