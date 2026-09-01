module carven:semantic.analysis.validation.flow.impl;

import :semantic.analysis.validation.context;
import :semantic.hir;
import std;

namespace {

auto match_coverage_aligned(
    std::span<const HIRMatchArm> arms,
    const HIRMatchCoverageFacts& coverage
) noexcept -> bool {
    return arms.size() == coverage.arm_states.size();
}

} // namespace

template<typename Program>
auto SemanticVerifier<Program>::check_flow() noexcept -> std::expected<void, SemanticProgramError> {
    for (auto index = 0uz; index < input.program().expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& expression = input.program().expression(id);
        if (!verify_failure_summary(input.program().expression_control(id))
            || !verify_evaluation_effect(input.program().evaluation_effect(id))
            || (std::holds_alternative<HIRTryExpr>(expression.value)
                != input.program().try_facts(id).has_value())) {
            break;
        }
        const auto aligned = std::visit(
            Overloaded {
                [&](const HIRMatchExpr& match) noexcept {
                    return match_coverage_aligned(match.arms, match.coverage);
                },
                [&](const HIRTryExpr& attempt) noexcept {
                    return input.program().try_facts(id)->arms.size() == attempt.arms.size();
                },
                [](const auto&) static noexcept { return true; },
            },
            expression.value
        );
        if (!aligned) {
            static_cast<void>(fail(
                SemanticProgramErrorKind::InvalidContract,
                "control-flow facts do not align with expression structure"
            ));
            break;
        }
    }
    if (!failure.has_value()) {
        for (const auto& statement : input.program().statements()) {
            const auto aligned = std::visit(
                Overloaded {
                    [](const HIRMatchStmt& match) noexcept {
                        return match_coverage_aligned(match.arms, match.coverage);
                    },
                    [](const auto&) static noexcept { return true; },
                },
                statement.value
            );
            if (!aligned) {
                static_cast<void>(fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "match coverage facts do not align with statement arms"
                ));
                break;
            }
        }
    }
    if (!failure.has_value()) {
        for (const auto& control : input.program().block_controls()) {
            if (!verify_failure_summary(control)) {
                break;
            }
        }
    }
    if (failure.has_value()) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

template<typename Program>
auto SemanticVerifier<Program>::verify_failure_summary(const HIRExpressionControl& control) noexcept
    -> bool {
    return failure_set_known(control.evaluation_failure_set);
}

template<typename Program>
auto SemanticVerifier<Program>::verify_effect_roots(std::span<const SymbolID> roots) noexcept
    -> bool {
    auto previous = std::optional<SymbolID>();
    for (const auto root : roots) {
        if (!symbol_known(root) || !input.program().binding(root).has_value()) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "evaluation effect references an unknown runtime binding"
            );
        }
        if (previous.has_value() && previous->index() >= root.index()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "evaluation effect roots are not unique and canonically ordered"
            );
        }
        previous = root;
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_evaluation_effect(const EvaluationEffect& effect) noexcept
    -> bool {
    return verify_effect_roots(effect.reads)
        && verify_effect_roots(effect.writes)
        && verify_effect_roots(effect.takes);
}

template<typename Program>
auto SemanticVerifier<Program>::verify_failure_summary(const HIRBlockControl& control) noexcept
    -> bool {
    return failure_set_known(control.outward_failure_set);
}

template<typename Program>
auto SemanticVerifier<Program>::visit_statement(HIRStmtID id, SemanticScopeID scope) noexcept
    -> bool {
    if (!semantic_id_known(id, input.program().statements().size())) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "program statement occurrence has an invalid identity"
        );
    }
    const auto& statement = input.program().statement(id);
    const auto valid = std::visit(
        Overloaded {
            [&](const HIRReturnStmt& returned) noexcept {
                return !returned.value.has_value() || visit_expression(*returned.value, scope);
            },
            [](const HIRBreakStmt&) static noexcept { return true; },
            [](const HIRContinueStmt&) static noexcept { return true; },
            [&](const HIRThrowStmt& thrown) noexcept {
                return type_known(thrown.failure_type)
                    && visit_expression(thrown.value, scope)
                    && input.program().expression(thrown.value).type == thrown.failure_type;
            },
            [](const HIRRethrowStmt&) static noexcept { return true; },
            [&](const HIRExprStmt& value) noexcept {
                return visit_expression(value.expression, scope);
            },
            [&](const HIRBindingStmt& binding) noexcept {
                if (!type_known(binding.type)) {
                    return fail(
                        SemanticProgramErrorKind::InvalidType,
                        "binding statement references an unknown type"
                    );
                }
                if (!binding_place(binding.target, binding.type, scope)
                    || !visit_expression(binding.initializer, scope)) {
                    return false;
                }
                return input.program().expression(binding.initializer).type == binding.type
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "binding initializer does not have the binding type"
                    );
            },
            [&](const HIRAssignmentStmt& assignment) noexcept {
                return visit_expression(assignment.target, scope)
                    && visit_expression(assignment.value, scope)
                    && input.program().expression(assignment.target).type
                    == input.program().expression(assignment.value).type;
            },
            [&](const HIRUpdateStmt& update) noexcept {
                return visit_expression(update.target, scope);
            },
            [&](const HIRIfStmt& conditional) noexcept {
                for (const auto& branch : conditional.branches) {
                    if (!visit_expression(branch.condition, scope)
                        || !visit_block(branch.body, scope)) {
                        return false;
                    }
                }
                return !conditional.else_branch.has_value()
                    || visit_block(*conditional.else_branch, scope);
            },
            [&](const HIRMatchStmt& match) noexcept {
                if (!visit_expression(match.subject, scope)) {
                    return false;
                }
                const auto subject = input.program().expression(match.subject).type;
                for (const auto& arm : match.arms) {
                    if (!visit_match_arm(arm, subject, scope)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const HIRWhileStmt& loop) noexcept {
                return visit_expression(loop.condition, scope) && visit_block(loop.body, scope);
            },
            [&](const HIRCStyleForStmt& loop) noexcept {
                if (!scope_known(loop.scope) || !scope_contains(scope, loop.scope)) {
                    return false;
                }
                if (loop.initializer.has_value()
                    && !visit_statement(*loop.initializer, loop.scope)) {
                    return false;
                }
                if (loop.condition.has_value() && !visit_expression(*loop.condition, loop.scope)) {
                    return false;
                }
                for (const auto step : loop.steps) {
                    if (!visit_statement(step, loop.scope)) {
                        return false;
                    }
                }
                return visit_block(loop.body, loop.scope);
            },
            [&](const HIRRangeForStmt& loop) noexcept {
                if (!scope_known(loop.scope)
                    || !scope_contains(scope, loop.scope)
                    || !type_known(loop.type)
                    || !binding_place(loop.target, loop.type, loop.scope)) {
                    return false;
                }
                const auto iterable = std::visit(
                    Overloaded {
                        [&](HIRExprID expression) noexcept {
                            return visit_expression(expression, loop.scope);
                        },
                        [&](const HIRHalfOpenRange& range) noexcept {
                            return visit_expression(range.begin, loop.scope)
                                && visit_expression(range.end, loop.scope)
                                && input.program().expression(range.begin).type
                                == input.program().expression(range.end).type;
                        },
                    },
                    loop.iterable
                );
                return iterable && visit_block(loop.body, loop.scope);
            },
            [&](const HIRTestCheckStmt& test) noexcept {
                return visit_expression(test.condition, scope)
                    && (!test.message.has_value() || visit_expression(*test.message, scope));
            },
            [&](const HIRTestRequireStmt& test) noexcept {
                return visit_expression(test.condition, scope)
                    && (!test.message.has_value() || visit_expression(*test.message, scope));
            },
            [&](const HIRTestFailStmt& test) noexcept {
                return !test.message.has_value() || visit_expression(*test.message, scope);
            },
            [](const HIRCppStmt&) static noexcept { return true; },
        },
        statement.value
    );
    if (!valid) {
        return failure.has_value() ? false
                                   : fail(
                                         SemanticProgramErrorKind::InvalidContract,
                                         "program statement form violates its structured shape"
                                     );
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::visit_block(HIRBlockID id, SemanticScopeID owner_scope) noexcept
    -> bool {
    if (!semantic_id_known(id, input.program().blocks().size())) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "program block occurrence has an invalid identity"
        );
    }
    const auto& block = input.program().block(id);
    if (!scope_known(block.scope) || !scope_contains(owner_scope, block.scope)) {
        return fail(
            SemanticProgramErrorKind::InvalidScope,
            "program block enters an invalid lexical scope"
        );
    }
    for (const auto statement : block.statements) {
        if (!visit_statement(statement, block.scope)) {
            return false;
        }
    }
    if (block.result.has_value() && !visit_expression(*block.result, block.scope)) {
        return false;
    }
    return true;
}

template class SemanticVerifier<SemanticProgramView>;
template class SemanticVerifier<SemanticDraftView>;

auto validate_flow(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).check_flow();
}

auto validate_flow(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).check_flow();
}
