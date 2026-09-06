module carven:semantic.analysis.body.failure.impl;

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

namespace body_elaboration {

auto BodyElaborator::build_try(
    const ASTTryForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    bool value_form
) noexcept -> AnalysisResult<std::optional<BuiltExpression>> {
    struct CatchCoverageAlternative final {
        std::optional<TypeID> failure_type;
        std::optional<PatternID> pattern;
        std::optional<ASTPatternID> source_pattern;
    };
    struct CatchCoverageSourceArm final {
        std::vector<CatchCoverageAlternative> alternatives;
        bool guarded;
    };

    const auto try_origin = origin(span);
    const auto outer_failure = failure_context_for_current_path();
    const auto protected_failures = draft().add_empty_failure_term();
    auto pending = PendingFailureTerms();
    auto result_type = expected;
    push_frame(ast.branch_block(source.body).span);
    reachable = true;
    failure_contexts.push_back({protected_failures, true});
    auto protected_body = build_branch(source.body, value_form, result_type, pending);
    failure_contexts.pop_back();
    if (!protected_body.has_value()) {
        return std::unexpected(protected_body.error());
    }
    auto normal = reachable;
    pop_frame();

    auto arms = std::vector<SemCatchArm>();
    arms.reserve(source.arms.size());
    auto coverage_history = std::vector<CatchCoverageSourceArm>();
    auto coverage_types = std::flat_set<TypeID>();
    auto catch_all_covered = false;
    auto incoming_failures = protected_failures;
    for (const auto& arm : source.arms) {
        push_frame(arm.span);
        auto bindings = std::flat_map<std::string, PatternBindingStorage, std::less<>>();
        auto expected_names = std::optional<std::flat_set<std::string, std::less<>>>();
        auto alternatives = std::vector<SemCatchAlternative>();
        auto coverage_alternatives = std::vector<CatchCoverageAlternative>();
        auto accepted_pieces = std::vector<FailureTermID>();
        auto catches_all = false;
        for (const auto& alternative : arm.pattern.alternatives) {
            auto alternative_names = std::flat_set<std::string, std::less<>>();
            auto semantic_pattern =
                std::variant<CatchAllPattern, SemTypedCatchPattern> {CatchAllPattern {}};
            auto accepted_piece = incoming_failures;
            auto coverage_pattern = std::optional<PatternID>();
            auto coverage_source_pattern = std::optional<ASTPatternID>();
            auto coverage_type = std::optional<TypeID>();
            if (const auto* typed = std::get_if<ASTCatchTypedPattern>(&alternative.value)) {
                auto failure_type = resolve_type(typed->type);
                if (!failure_type.has_value()) {
                    return std::unexpected(failure_type.error());
                }
                const auto* concrete = std::get_if<TypeID>(&*failure_type);
                if (concrete == nullptr || !is_failure_payload_type(draft(), *concrete)) {
                    return std::unexpected(fail(
                        alternative.span,
                        DiagnosticCode::EffectThrowType,
                        "catch alternatives require a concrete nominal failure type"
                    ));
                }
                auto pattern = build_pattern(
                    typed->inner,
                    *failure_type,
                    bindings,
                    !expected_names.has_value(),
                    alternative_names
                );
                if (!pattern.has_value()) {
                    return std::unexpected(pattern.error());
                }
                coverage_pattern = pattern->pattern;
                coverage_source_pattern = typed->inner;
                coverage_type = *concrete;
                coverage_types.insert(*concrete);
                semantic_pattern = SemTypedCatchPattern {
                    .type = BodyType(*failure_type),
                    .inner = pattern->pattern,
                };
                accepted_piece =
                    draft().add_intersection_failure_term(incoming_failures, {*concrete});
            } else {
                catches_all = true;
            }
            if (!expected_names.has_value()) {
                expected_names = alternative_names;
            } else if (*expected_names != alternative_names) {
                return std::unexpected(fail(
                    alternative.span,
                    DiagnosticCode::MatchBindingMismatch,
                    "catch alternatives must bind the same names"
                ));
            }
            accepted_pieces.push_back(accepted_piece);
            alternatives.push_back(
                SemCatchAlternative {
                    .origin = origin(alternative.span),
                    .pattern = semantic_pattern,
                    .reachable = false,
                }
            );
            coverage_alternatives.push_back(
                CatchCoverageAlternative {
                    .failure_type = coverage_type,
                    .pattern = coverage_pattern,
                    .source_pattern = coverage_source_pattern,
                }
            );
        }
        if (arm.pattern.alternatives.empty()) {
            invariant_violation("catch arm has no alternatives");
        }
        coverage_history.push_back(
            CatchCoverageSourceArm {
                .alternatives = std::move(coverage_alternatives),
                .guarded = false,
            }
        );
        auto alternative_useful = std::vector<bool>(alternatives.size(), false);
        auto exhaustive_types = std::vector<TypeID>();
        for (const auto failure_type : coverage_types) {
            struct CoverageSourceAlternative final {
                std::size_t catch_alternative;
                std::optional<std::size_t> inner_alternative;
            };
            auto coverage_arms = std::vector<PatternCoverageArm>();
            auto source_arms = std::vector<std::size_t>();
            auto source_alternatives = std::vector<std::vector<CoverageSourceAlternative>>();
            for (auto arm_index = 0uz; arm_index < coverage_history.size(); ++arm_index) {
                auto patterns = std::vector<std::optional<PatternID>>();
                auto indices = std::vector<CoverageSourceAlternative>();
                for (auto alternative_index = 0uz;
                     alternative_index < coverage_history[arm_index].alternatives.size();
                     ++alternative_index) {
                    const auto& candidate =
                        coverage_history[arm_index].alternatives[alternative_index];
                    if (candidate.failure_type.has_value()
                        && *candidate.failure_type != failure_type) {
                        continue;
                    }
                    if (candidate.pattern.has_value()) {
                        const auto semantic = body_builder.pattern_copy(*candidate.pattern);
                        if (const auto* disjunction = std::get_if<OrPattern>(&semantic.value)) {
                            for (auto inner = 0uz; inner < disjunction->alternatives.size();
                                 ++inner) {
                                patterns.emplace_back(disjunction->alternatives[inner]);
                                indices.push_back(
                                    CoverageSourceAlternative {
                                        .catch_alternative = alternative_index,
                                        .inner_alternative = inner,
                                    }
                                );
                            }
                            continue;
                        }
                    }
                    patterns.push_back(candidate.pattern);
                    indices.push_back(
                        CoverageSourceAlternative {
                            .catch_alternative = alternative_index,
                            .inner_alternative = std::nullopt,
                        }
                    );
                }
                if (patterns.empty()) {
                    continue;
                }
                coverage_arms.push_back(
                    PatternCoverageArm {
                        .alternatives = std::move(patterns),
                        .guarded = coverage_history[arm_index].guarded,
                    }
                );
                source_arms.push_back(arm_index);
                source_alternatives.push_back(std::move(indices));
            }
            auto coverage = compute_pattern_coverage(
                draft(),
                body_builder.pattern_table(),
                failure_type,
                coverage_arms
            );
            if (!coverage.has_value()) {
                return std::unexpected(fail(
                    arm.pattern.span,
                    DiagnosticCode::TypeMatchPattern,
                    std::format("invalid catch pattern coverage: {}", coverage.error())
                ));
            }
            for (const auto redundant : coverage->redundant_alternatives) {
                const auto source_arm = source_arms[redundant.arm];
                if (source_arm + 1uz != coverage_history.size()) {
                    continue;
                }
                const auto source_alternative =
                    source_alternatives[redundant.arm][redundant.alternative];
                auto diagnostic_span =
                    arm.pattern.alternatives[source_alternative.catch_alternative].span;
                if (source_alternative.inner_alternative.has_value()) {
                    const auto source_pattern =
                        coverage_history.back()
                            .alternatives[source_alternative.catch_alternative]
                            .source_pattern;
                    if (source_pattern.has_value()) {
                        const auto* disjunction =
                            std::get_if<ASTOrPattern>(&ast.pattern(*source_pattern).value);
                        if (disjunction != nullptr
                            && *source_alternative.inner_alternative
                                < disjunction->alternatives.size()) {
                            diagnostic_span =
                                ast.pattern(
                                       disjunction
                                           ->alternatives[*source_alternative.inner_alternative]
                                )
                                    .span;
                        }
                    }
                }
                return std::unexpected(fail(
                    diagnostic_span,
                    DiagnosticCode::MatchDuplicateAlternative,
                    "catch or-pattern contains a repeated or subsumed alternative"
                ));
            }
            const auto current = std::ranges::find(source_arms, coverage_history.size() - 1uz);
            if (current == source_arms.end()) {
                continue;
            }
            const auto coverage_index =
                static_cast<std::size_t>(std::distance(source_arms.begin(), current));
            for (auto index = 0uz; index < source_alternatives[coverage_index].size(); ++index) {
                const auto source_alternative = source_alternatives[coverage_index][index];
                alternative_useful[source_alternative.catch_alternative] =
                    alternative_useful[source_alternative.catch_alternative]
                    || coverage->alternative_usefulness[coverage_index][index];
            }
            const auto previously_exhaustive =
                coverage_index != 0uz && coverage->exhaustive_after_arm[coverage_index - 1uz];
            if (!previously_exhaustive && coverage->exhaustive_after_arm[coverage_index]) {
                exhaustive_types.push_back(failure_type);
            }
        }
        for (auto index = 0uz; index < alternatives.size(); ++index) {
            if (!coverage_history.back().alternatives[index].failure_type.has_value()) {
                alternative_useful[index] = alternative_useful[index] || !catch_all_covered;
            }
            alternatives[index].reachable = alternative_useful[index];
        }
        std::ranges::sort(exhaustive_types, {}, &TypeID::index);
        exhaustive_types.erase(
            std::ranges::unique(exhaustive_types).begin(),
            exhaustive_types.end()
        );
        auto useful_pieces = std::vector<FailureTermID>();
        useful_pieces.reserve(accepted_pieces.size());
        for (auto index = 0uz; index < accepted_pieces.size(); ++index) {
            if (alternatives[index].reachable) {
                useful_pieces.push_back(accepted_pieces[index]);
            }
        }
        const auto accepted = draft().add_union_failure_term(std::move(useful_pieces));
        const auto residual = catches_all
            ? draft().add_empty_failure_term()
            : draft().add_residual_failure_term(incoming_failures, std::move(exhaustive_types));
        const auto arm_useful = std::ranges::any_of(
            alternatives,
            [](const SemCatchAlternative& alternative) static noexcept {
                return alternative.reachable;
            }
        );

        auto produced_bindings = std::vector<LocalBindingID>();
        for (const auto& [name, binding] : bindings) {
            static_cast<void>(name);
            produced_bindings.push_back(binding.storage.binding);
        }
        std::ranges::sort(produced_bindings, {}, &LocalBindingID::index);
        [[maybe_unused]] const auto path = ReferencePathGuard(reference_path_reachable, arm_useful);
        reachable = true;
        [[maybe_unused]] const auto suspension = FullExpressionSuspension(active_full_expression);
        const auto local_failures = draft().add_empty_failure_term();
        if (reference_path_reachable) {
            draft().add_guarded_failure_contribution(outer_failure.term, accepted, local_failures);
        }
        const auto local_failure =
            FailureContext {local_failures, outer_failure.accepts_catch_residual};
        failure_contexts.push_back(local_failure);
        catches.push_back({accepted, local_failure});
        auto arm_pending = PendingFailureTerms();
        auto guard_tree = std::optional<SemanticExpression>();
        auto body_reachable = true;
        auto guard_may_reject = false;
        if (arm.guard.has_value()) {
            const auto id = arm.guard->expression;
            auto guard = expression(id, draft().intern_builtin_type(BuiltinType::Bool));
            if (!guard.has_value()) {
                return std::unexpected(guard.error());
            }
            if (value_form) {
                collect_pending(arm_pending, *guard);
            }
            const auto known = known_boolean_constant(draft(), guard->constant());
            auto checked = require_bool(*guard, ast.expression(id).span);
            if (!checked.has_value()) {
                return std::unexpected(checked.error());
            }
            guard_tree = std::move(*checked);
            body_reachable = guard->completes && (!known.has_value() || *known);
            guard_may_reject = guard->completes && known != true;
        }
        auto body = [&]() noexcept -> AnalysisResult<SemanticRegion> {
            [[maybe_unused]] const auto body_path =
                ReferencePathGuard(reference_path_reachable, body_reachable);
            return build_arm(arm.body, value_form, result_type, arm_pending);
        }();
        if (!body.has_value()) {
            return std::unexpected(body.error());
        }
        for (const auto term : arm_pending) {
            const auto gated = draft().add_empty_failure_term();
            draft().add_guarded_failure_contribution(gated, accepted, term);
            append_pending(pending, {gated});
        }
        normal = normal || (arm_useful && body_reachable && reachable);
        catches.pop_back();
        failure_contexts.pop_back();
        arms.push_back(
            {origin(arm.span),
             BodyFailures(accepted),
             std::move(alternatives),
             std::move(produced_bindings),
             std::move(guard_tree),
             std::move(*body)}
        );
        coverage_history.back().guarded = guard_may_reject;
        catch_all_covered |= catches_all && !guard_may_reject;
        incoming_failures =
            guard_may_reject ? draft().add_union_failure_term({residual, accepted}) : residual;
        pop_frame();
    }

    if (!outer_failure.accepts_catch_residual) {
        draft().require_empty_failures(
            incoming_failures,
            try_origin,
            EmptyFailureRequirementKind::CatchResidual
        );
    }
    draft().add_failure_contribution(outer_failure.term, incoming_failures);
    reachable = normal;
    auto result = make_built(
        result_type.value_or(draft().intern_builtin_type(BuiltinType::Void)),
        SemTry {
            UniqueIndirect(std::move(*protected_body)),
            BodyFailures(protected_failures),
            BodyFailures(incoming_failures),
            std::move(arms)
        },
        span,
        std::move(pending)
    );
    if (!value_form) {
        append_expression(result, span);
        return std::optional<BuiltExpression>();
    }
    return std::optional(normal ? std::move(result) : mark_noncompleting(std::move(result)));
}

auto BodyElaborator::try_expression(
    const ASTTryForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<BuiltExpression> {
    auto built = build_try(source, span, expected, true);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    if (!built->has_value()) {
        invariant_violation("value try did not produce a value");
    }
    return std::move(**built);
}

auto BodyElaborator::try_statement(const ASTTryForm& source, Span span) noexcept
    -> AnalysisResult<void> {
    auto built = build_try(source, span, std::nullopt, false);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    return {};
}


} // namespace body_elaboration
