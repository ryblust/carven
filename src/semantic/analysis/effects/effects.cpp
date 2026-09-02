module carven:semantic.analysis.effects.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.analyzer;
import :semantic.analysis.control;
import :semantic.analysis.effects;
import :semantic.analysis.elaboration.types.relations;
import :semantic.analysis.session.read;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :source.location;
import :support.visit;
import std;

namespace {

auto diagnose_recorded_effects(
    SemanticDraftView builder,
    const CallableConstraints& callable_constraints,
    DiagnosticSink& diagnostics,
    const RecordedControlAnalysis& control
) noexcept -> void {
    const auto emit =
        [&](ProgramOriginID origin, std::string message, DiagnosticCode code) noexcept {
            diagnostics.emit(DiagnosticBuilder(code, std::move(message))
                                 .primary(diagnostic_span(builder, origin))
                                 .build());
        };
    const auto is_void = [&](HIRTypeID type) noexcept {
        const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&builder.type(type).value);
        return builtin != nullptr && builtin->kind == HIRBuiltinType::Void;
    };
    const auto callable_failures = [&](CallableID callable) noexcept {
        return control.effective_failures(callable);
    };
    for (const auto& constraint : callable_constraints.values()) {
        const auto target_type = std::visit(
            Overloaded {
                [](HIRTypeID type) static noexcept { return type; },
                [&](HIRExprID expression) noexcept { return builder.expression(expression).type; },
            },
            constraint.target
        );
        if (callable_adoption_compatible(
                builder,
                target_type,
                builder.expression(constraint.source).type,
                control.callable_failure_sets()
            )) {
            continue;
        }
        diagnostics.emit(DiagnosticBuilder(constraint.code, constraint.message)
                             .primary(diagnostic_span(builder, constraint.origin))
                             .build());
    }

    for (auto index = 0uz; index < builder.functions().size(); ++index) {
        const auto id = FunctionID::from_index(static_cast<std::uint32_t>(index));
        const auto& function = builder.function(id);
        const auto body_id = callable_body_id(builder.callable(function.callable));
        if (!body_id.has_value()) {
            continue;
        }
        const auto& body = builder.body(*body_id);
        const auto& actual = control.summary(body.root).outward_failures;
        const auto contract = callable_failures(function.callable);
        const auto outside = std::ranges::find_if(actual, [&](HIRTypeID failure) noexcept {
            return !std::ranges::contains(contract, failure);
        });
        if (outside != actual.end()) {
            const auto missing_declaration =
                builder.callable_failure_input(function.callable).policy
                == SemanticFailureContractKind::UndeclaredPublished;
            emit(
                function.origin,
                missing_declaration
                    ? "published function with failures requires an explicit 'throw' clause"
                    : "function body can produce a failure outside its declared contract",
                missing_declaration ? DiagnosticCode::EffectThrowPublished
                                    : DiagnosticCode::EffectSignatureBound
            );
        }
        if (function.entry_point.has_value() && (!actual.empty() || !contract.empty())) {
            emit(
                function.origin,
                "entry function must handle every failure and cannot declare a "
                "failure contract",
                DiagnosticCode::EffectRootUnhandled
            );
        }
    }
    for (const auto& hir_module : builder.modules()) {
        for (const auto& item : hir_module.items) {
            if (const auto* test_id = std::get_if<TestID>(&item); test_id != nullptr
                && !control.summary(builder.body(builder.test(*test_id).body).root)
                        .outward_failures.empty()) {
                emit(
                    builder.test(*test_id).origin,
                    "test body must handle every failure",
                    DiagnosticCode::EffectRootUnhandled
                );
            }
        }
    }

    for (auto index = 0uz; index < builder.expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& expression = builder.expression(id);
        const auto* propagation = std::get_if<HIRPropagationExpr>(&expression.value);
        if (propagation != nullptr
            && control.summary(propagation->operand_id).pending_failures.empty()) {
            emit(
                expression.origin,
                "'?' cannot propagate an infallible value",
                DiagnosticCode::EffectPropagateRedundant
            );
        }
    }

    const auto diagnose_unmarked_final = [&](HIRExprID id) noexcept {
        if (control.summary(id).pending_failures.empty()) {
            return;
        }
        emit(
            builder.expression(id).origin,
            "throwing expression requires explicit '?' propagation",
            DiagnosticCode::EffectUnmarked
        );
    };
    for (const auto& statement : builder.statements()) {
        std::visit(
            Overloaded {
                [&](const HIRReturnStmt& value) noexcept {
                    if (value.value.has_value()) {
                        diagnose_unmarked_final(*value.value);
                    }
                },
                [&](const HIRThrowStmt& value) noexcept { diagnose_unmarked_final(value.value); },
                [&](const HIRExprStmt& value) noexcept {
                    diagnose_unmarked_final(value.expression);
                },
                [&](const HIRBindingStmt& value) noexcept {
                    diagnose_unmarked_final(value.initializer);
                },
                [&](const HIRAssignmentStmt& value) noexcept {
                    diagnose_unmarked_final(value.target);
                    diagnose_unmarked_final(value.value);
                },
                [&](const HIRUpdateStmt& value) noexcept { diagnose_unmarked_final(value.target); },
                [&](const HIRIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        diagnose_unmarked_final(branch.condition);
                    }
                },
                [&](const HIRMatchStmt& value) noexcept {
                    diagnose_unmarked_final(value.subject);
                    for (const auto& arm : value.arms) {
                        if (arm.guard.has_value()) {
                            diagnose_unmarked_final(*arm.guard);
                        }
                    }
                },
                [&](const HIRWhileStmt& value) noexcept {
                    diagnose_unmarked_final(value.condition);
                },
                [&](const HIRCStyleForStmt& value) noexcept {
                    if (value.condition.has_value()) {
                        diagnose_unmarked_final(*value.condition);
                    }
                },
                [&](const HIRRangeForStmt& value) noexcept {
                    std::visit(
                        Overloaded {
                            [&](HIRExprID iterable) noexcept { diagnose_unmarked_final(iterable); },
                            [&](const HIRHalfOpenRange& range) noexcept {
                                diagnose_unmarked_final(range.begin);
                                diagnose_unmarked_final(range.end);
                            },
                        },
                        value.iterable
                    );
                },
                [&](const HIRTestCheckStmt& value) noexcept {
                    diagnose_unmarked_final(value.condition);
                    if (value.message.has_value()) {
                        diagnose_unmarked_final(*value.message);
                    }
                },
                [&](const HIRTestRequireStmt& value) noexcept {
                    diagnose_unmarked_final(value.condition);
                    if (value.message.has_value()) {
                        diagnose_unmarked_final(*value.message);
                    }
                },
                [&](const HIRTestFailStmt& value) noexcept {
                    if (value.message.has_value()) {
                        diagnose_unmarked_final(*value.message);
                    }
                },
                [](const auto&) static noexcept {},
            },
            statement.value
        );
    }

    for (auto index = 0uz; index < builder.blocks().size(); ++index) {
        const auto id = HIRBlockID::from_index(static_cast<std::uint32_t>(index));
        auto reachable = true;
        for (const auto statement_id : builder.block(id).statements) {
            if (!reachable) {
                emit(
                    builder.statement(statement_id).origin,
                    "unreachable statement",
                    DiagnosticCode::FlowUnreachable
                );
            }
            if (reachable) {
                reachable = control.summary(statement_id).falls_through;
            }
        }
    }

    const auto diagnose_attempt = [&](HIRExprID id,
                                      ProgramOriginID origin,
                                      HIRTypeID result,
                                      HIRBlockID body,
                                      std::span<const HIRCatchArm> arms) noexcept {
        const auto& protected_failures = control.summary(body).outward_failures;
        const auto value_form = builder.block(body).result.has_value();
        for (auto arm_index = 0uz; arm_index < arms.size(); ++arm_index) {
            const auto& arm = arms[arm_index];
            if (value_form
                && !is_void(result)
                && !builder.block(arm.body).result.has_value()
                && control.summary(arm.body).falls_through) {
                emit(
                    builder.block(arm.body).origin,
                    "value catch arm must end with a result expression",
                    DiagnosticCode::FlowValueBranchResult
                );
            }
            if (protected_failures.empty()) {
                continue;
            }
            const auto& catch_summary = control.catch_summary(id, arm_index);
            const auto arm_reachable = std::ranges::contains(
                catch_summary.alternatives,
                CatchAlternativeReachability::Reachable
            );
            if (!arm_reachable) {
                emit(
                    arm.origin,
                    "catch arm cannot match a remaining protected failure",
                    DiagnosticCode::EffectCatchArmUnreachable
                );
                continue;
            }
            for (auto alternative_index = 0uz; alternative_index < arm.alternatives.size();
                 ++alternative_index) {
                const auto reachability = catch_summary.alternatives[alternative_index];
                if (reachability == CatchAlternativeReachability::Reachable) {
                    continue;
                }
                emit(
                    arm.alternatives[alternative_index].origin,
                    reachability == CatchAlternativeReachability::FailureAbsent
                        ? "catch alternative names a failure not produced by the protected body"
                        : "catch alternative is covered by earlier unguarded alternatives",
                    DiagnosticCode::EffectCatchAlternativeUnreachable
                );
            }
        }
        if (!control.unhandled_failures(id).empty()) {
            emit(
                origin,
                "catch does not cover every protected failure",
                DiagnosticCode::EffectCatchNonExhaustive
            );
        }
    };
    for (auto index = 0uz; index < builder.expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& expression = builder.expression(id);
        if (const auto* attempt = std::get_if<HIRTryExpr>(&expression.value)) {
            diagnose_attempt(id, expression.origin, expression.type, attempt->body, attempt->arms);
        }
        const auto* closure = std::get_if<HIRClosureExpr>(&expression.value);
        if (closure == nullptr) {
            continue;
        }
        const auto& callable = builder.callable(closure->callable);
        const auto& closure_body = builder.body(*callable_body_id(callable));
        const auto& actual = control.summary(closure_body.root).outward_failures;
        const auto contract = callable_failures(closure->callable);
        if (std::ranges::any_of(actual, [&](HIRTypeID failure) noexcept {
                return !std::ranges::contains(contract, failure);
            })) {
            emit(
                expression.origin,
                "lambda body exceeds its declared failure contract",
                DiagnosticCode::EffectSignatureBound
            );
        }
        if (!is_void(closure->result) && control.summary(closure_body.root).falls_through) {
            emit(
                expression.origin,
                "not all paths return a value",
                DiagnosticCode::FlowMissingReturn
            );
        }
    }
    for (auto index = 0uz; index < builder.functions().size(); ++index) {
        const auto id = FunctionID::from_index(static_cast<std::uint32_t>(index));
        const auto& function = builder.function(id);
        const auto body_id = callable_body_id(builder.callable(function.callable));
        if (!body_id.has_value()) {
            continue;
        }
        const auto& body = builder.body(*body_id);
        if (is_void(function.result) || !control.summary(body.root).falls_through) {
            continue;
        }
        emit(
            builder.block(body.root).origin,
            "not all paths return a value",
            DiagnosticCode::FlowMissingReturn
        );
    }
}

} // namespace

auto diagnose_effects(
    SemanticDraftView builder,
    const CallableConstraints& callable_constraints,
    DiagnosticSink& diagnostics,
    const RecordedControlAnalysis& control
) noexcept -> void {
    diagnose_recorded_effects(builder, callable_constraints, diagnostics, control);
}
