module carven:semantic.analysis.body.pattern.impl;

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

auto BodyElaborator::build_pattern(
    ASTPatternID source_id,
    ConstructionTypeRef type,
    std::flat_map<std::string, BodyPatternBindingStorage, std::less<>>& bindings,
    bool allow_new_bindings,
    std::flat_set<std::string, std::less<>>& used_bindings
) noexcept -> AnalysisResult<BuiltPattern> {
    const auto& source = ast.pattern(source_id);
    const auto pattern_origin = origin(source.span);
    const auto add = [&](ElaboratedPatternValue value) noexcept {
        return body_builder.add_pattern(
            ElaboratedPattern {
                .type = type,
                .value = std::move(value),
                .origin = pattern_origin,
            }
        );
    };
    return std::visit(
        Overloaded {
            [&](const ASTWildcardPattern&) noexcept -> AnalysisResult<BuiltPattern> {
                return BuiltPattern {
                    .pattern = add(WildcardPattern {}),
                    .bindings = {},
                    .irrefutable = true,
                };
            },
            [&](const ASTLiteral& literal) noexcept -> AnalysisResult<BuiltPattern> {
                auto normalized = normalize_literal(draft(), literal, type);
                if (!normalized.has_value()) {
                    const auto diagnostic = constant_evaluation_diagnostic(normalized.error());
                    return std::unexpected(fail(
                        source.span,
                        diagnostic.has_value() ? diagnostic->code
                                               : DiagnosticCode::TypeMatchPattern,
                        diagnostic.has_value() ? std::string(diagnostic->message)
                                               : "pattern literal is incompatible with its subject"
                    ));
                }
                const auto constant = draft().intern_constant(std::move(*normalized));
                return BuiltPattern {
                    .pattern = add(LiteralPattern {.constant = constant}),
                    .bindings = {},
                    .irrefutable = false,
                };
            },
            [&](const ASTNegativeNumberPattern& negative) noexcept -> AnalysisResult<BuiltPattern> {
                auto normalized = std::visit(
                    [&]<typename Numeric>(const Numeric& numeric) noexcept {
                        static_assert(
                            std::same_as<Numeric, IntegerLiteralValue>
                                || std::same_as<Numeric, FloatingLiteralValue>,
                            "unhandled negative numeric pattern literal"
                        );
                        return normalize_literal(
                            draft(),
                            ASTLiteral {.span = negative.number_span, .value = numeric},
                            type,
                            LiteralSign::Negative
                        );
                    },
                    negative.value
                );
                if (!normalized.has_value()) {
                    const auto diagnostic = constant_evaluation_diagnostic(normalized.error());
                    return std::unexpected(fail(
                        source.span,
                        diagnostic.has_value() ? diagnostic->code
                                               : DiagnosticCode::TypeMatchPattern,
                        diagnostic.has_value() ? std::string(diagnostic->message)
                                               : "negative pattern is incompatible with its subject"
                    ));
                }
                const auto constant = draft().intern_constant(std::move(*normalized));
                return BuiltPattern {
                    .pattern = add(LiteralPattern {.constant = constant}),
                    .bindings = {},
                    .irrefutable = false,
                };
            },
            [&](const ASTBindingPattern& binding) noexcept -> AnalysisResult<BuiltPattern> {
                const auto name = spelling(binding.name_span);
                if (!used_bindings.insert(name).second) {
                    return std::unexpected(fail(
                        binding.name_span,
                        DiagnosticCode::NameDuplicateLocal,
                        std::format("pattern binds '{}' more than once", name)
                    ));
                }
                auto found = bindings.find(name);
                if (found == bindings.end()) {
                    if (!allow_new_bindings) {
                        return std::unexpected(fail(
                            binding.name_span,
                            DiagnosticCode::MatchBindingMismatch,
                            "or-pattern alternatives must bind the same names"
                        ));
                    }
                    const auto storage = body_builder.add_owner_binding(
                        draft().intern_spelling(name),
                        type,
                        frames.back().lifetime,
                        false,
                        origin(binding.name_span)
                    );
                    auto published = bind_local(
                        binding.name_span,
                        BodyLocalStorage {
                            .storage = storage,
                            .type = type,
                            .unused_candidate = std::nullopt,
                        },
                        DiagnosticCode::MatchBindingMismatch
                    );
                    if (!published.has_value()) {
                        return std::unexpected(published.error());
                    }
                    found = bindings
                                .emplace(
                                    name,
                                    BodyPatternBindingStorage {.storage = storage, .type = type}
                                )
                                .first;
                } else if (!compatible(found->second.type, type)) {
                    return std::unexpected(fail(
                        binding.name_span,
                        DiagnosticCode::MatchBindingMismatch,
                        "or-pattern binding has a different type in another alternative"
                    ));
                }
                return BuiltPattern {
                    .pattern = add(BindingPattern {.binding = found->second.storage.binding}),
                    .bindings = {found->second.storage.binding},
                    .irrefutable = true,
                };
            },
            [&](const ASTConstraintPattern& constraint) noexcept -> AnalysisResult<BuiltPattern> {
                auto constrained = resolve_pattern_constraint(constraint.operand);
                if (!constrained.has_value()) {
                    return std::unexpected(constrained.error());
                }
                if (!compatible(type, *constrained)) {
                    return std::unexpected(fail(
                        constraint.operand.span,
                        DiagnosticCode::TypeMatchConstraint,
                        "type constraint is incompatible with the match subject"
                    ));
                }
                return BuiltPattern {
                    .pattern = add(ElaboratedTypeConstraintPattern {.type = *constrained}),
                    .bindings = {},
                    .irrefutable = true,
                };
            },
            [&](const ASTCasePattern& case_pattern) noexcept -> AnalysisResult<BuiltPattern> {
                const auto* concrete = std::get_if<TypeID>(&type);
                if (concrete == nullptr) {
                    return std::unexpected(fail(
                        source.span,
                        DiagnosticCode::TypeMatchPattern,
                        "enum case pattern requires a concrete enum subject"
                    ));
                }
                const auto canonical = draft().type_copy(*concrete);
                const auto* subject_enum = std::get_if<EnumTypeValue>(&canonical.value);
                if (subject_enum == nullptr) {
                    return std::unexpected(fail(
                        source.span,
                        DiagnosticCode::TypeMatchPattern,
                        "case pattern requires an enum subject"
                    ));
                }
                const auto owner = subject_enum->enumeration;
                if (const auto* qualified =
                        std::get_if<ASTQualifiedCaseQualifier>(&case_pattern.qualifier)) {
                    if (qualified->components.empty()) {
                        invariant_violation("qualified case pattern has no qualifier");
                    }
                    const auto qualifier_name = spelling(qualified->components.back());
                    auto selected = find_global(qualifier_name, qualified->components.back());
                    if (!selected.has_value()) {
                        return std::unexpected(selected.error());
                    }
                    const auto* enumeration = std::get_if<CatalogEnumForm>(&(*selected)->form);
                    if (enumeration == nullptr || enumeration->enumeration != owner) {
                        return std::unexpected(fail(
                            qualified->span,
                            DiagnosticCode::TypeMatchPattern,
                            "case pattern qualifier differs from the subject enum"
                        ));
                    }
                }
                const auto declaration = draft().enum_declaration_copy(owner);
                const auto case_name = spelling(case_pattern.name_span);
                auto selected_id = std::optional<EnumCaseID>();
                auto selected = std::optional<ConstructionEnumCaseDeclaration>();
                for (const auto case_id : declaration.cases) {
                    auto candidate = draft().construction_enum_case_declaration_copy(case_id);
                    if (draft().spelling_copy(candidate.name) == case_name) {
                        selected_id = case_id;
                        selected = std::move(candidate);
                        break;
                    }
                }
                if (!selected.has_value()) {
                    return std::unexpected(fail(
                        case_pattern.name_span,
                        DiagnosticCode::TypeMatchPattern,
                        std::format("enum has no case named '{}'", case_name)
                    ));
                }
                const auto payload_ids = case_pattern.payload.has_value()
                    ? std::span<const ASTPatternID>(case_pattern.payload->patterns)
                    : std::span<const ASTPatternID>();
                if (payload_ids.size() != selected->payload_types.size()) {
                    return std::unexpected(fail(
                        source.span,
                        DiagnosticCode::TypeEnumCaseArity,
                        "enum case pattern payload arity does not match"
                    ));
                }
                auto payload = std::vector<PatternID>();
                auto result_bindings = std::vector<LocalBindingID>();
                for (auto index = 0uz; index < payload_ids.size(); ++index) {
                    auto child = build_pattern(
                        payload_ids[index],
                        selected->payload_types[index],
                        bindings,
                        allow_new_bindings,
                        used_bindings
                    );
                    if (!child.has_value()) {
                        return std::unexpected(child.error());
                    }
                    payload.push_back(child->pattern);
                    result_bindings.insert(
                        result_bindings.end(),
                        child->bindings.begin(),
                        child->bindings.end()
                    );
                }
                std::ranges::sort(result_bindings, {}, &LocalBindingID::index);
                return BuiltPattern {
                    .pattern =
                        add(EnumCasePattern {
                            .enum_case = *selected_id,
                            .payload = std::move(payload),
                        }),
                    .bindings = std::move(result_bindings),
                    .irrefutable = false,
                };
            },
            [&](const ASTOrPattern& or_pattern) noexcept -> AnalysisResult<BuiltPattern> {
                if (or_pattern.alternatives.empty()) {
                    invariant_violation("or-pattern has no alternatives");
                }
                auto alternatives = std::vector<PatternID>();
                auto expected_names = std::optional<std::flat_set<std::string, std::less<>>>();
                auto result_bindings = std::vector<LocalBindingID>();
                auto irrefutable = false;
                for (auto index = 0uz; index < or_pattern.alternatives.size(); ++index) {
                    auto alternative_names = std::flat_set<std::string, std::less<>>();
                    auto alternative = build_pattern(
                        or_pattern.alternatives[index],
                        type,
                        bindings,
                        index == 0uz && allow_new_bindings,
                        alternative_names
                    );
                    if (!alternative.has_value()) {
                        return std::unexpected(alternative.error());
                    }
                    if (!expected_names.has_value()) {
                        expected_names = alternative_names;
                    } else if (*expected_names != alternative_names) {
                        return std::unexpected(fail(
                            ast.pattern(or_pattern.alternatives[index]).span,
                            DiagnosticCode::MatchBindingMismatch,
                            "or-pattern alternatives must bind the same names"
                        ));
                    }
                    alternatives.push_back(alternative->pattern);
                    irrefutable |= alternative->irrefutable;
                }
                for (const auto& name : *expected_names) {
                    if (!used_bindings.insert(name).second) {
                        return std::unexpected(fail(
                            source.span,
                            DiagnosticCode::NameDuplicateLocal,
                            std::format("pattern binds '{}' more than once", name)
                        ));
                    }
                }
                for (const auto& name : *expected_names) {
                    result_bindings.push_back(bindings.at(name).storage.binding);
                }
                std::ranges::sort(result_bindings, {}, &LocalBindingID::index);
                return BuiltPattern {
                    .pattern = add(OrPattern {.alternatives = std::move(alternatives)}),
                    .bindings = std::move(result_bindings),
                    .irrefutable = irrefutable,
                };
            },
        },
        source.value
    );
}

auto BodyElaborator::resolve_pattern_constraint(const ASTConstraintOperand& operand) noexcept
    -> AnalysisResult<ConstructionTypeRef> {
    return resolve_source_constraint_type(
        draft(),
        catalog(),
        import_usage(),
        source_module_id,
        ast,
        operand,
        [&](ASTExprID extent) noexcept { return resolve_array_extent(extent); }
    );
}

auto BodyElaborator::build_match(
    const ASTMatchForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    bool value_form,
    bool allow_pointer_narrowing
) noexcept -> AnalysisResult<std::optional<BuiltExpression>> {
    auto subject = expression(source.subject);
    if (!subject.has_value()) {
        return std::unexpected(subject.error());
    }
    auto pending = BodyPendingFailureTerms();
    auto result_type = expected;
    if (value_form) {
        collect_pending(pending, *subject);
    }
    auto consumed = consume_pending(*subject, ast.expression(source.subject).span);
    if (!consumed.has_value()) {
        return std::unexpected(consumed.error());
    }
    const auto selection_reachable = reachable;
    const auto subject_type = subject->type();
    if (is_void_type(draft(), subject_type)) {
        return std::unexpected(fail(
            ast.expression(source.subject).span,
            DiagnosticCode::TypeValueRequired,
            "void expression cannot be used as a value"
        ));
    }
    const auto subject_is_place = std::holds_alternative<PlaceExpression>(subject->storage);
    auto subject_tree = take_built(*subject, ast.expression(source.subject).span);
    if (source.arms.empty()) {
        return std::unexpected(fail(span, DiagnosticCode::MatchNonExhaustive, "match has no arms"));
    }

    struct ArmPlan final {
        const ASTMatchArm* source;
        BodyLocalFrame frame;
        BuiltPattern pattern;
        bool useful;
    };

    auto plans = std::vector<ArmPlan>();
    plans.reserve(source.arms.size());

    for (const auto& arm : source.arms) {
        push_frame(arm.span);
        auto bindings = std::flat_map<std::string, BodyPatternBindingStorage, std::less<>>();
        auto used = std::flat_set<std::string, std::less<>>();
        auto pattern = build_pattern(arm.pattern, subject_type, bindings, true, used);
        if (!pattern.has_value()) {
            return std::unexpected(pattern.error());
        }
        pattern->bindings.clear();
        for (const auto& [name, binding] : bindings) {
            static_cast<void>(name);
            pattern->bindings.push_back(binding.storage.binding);
        }
        std::ranges::sort(pattern->bindings, {}, &LocalBindingID::index);

        auto frame = std::move(frames.back());
        pop_frame(false);
        plans.push_back(
            ArmPlan {
                .source = std::addressof(arm),
                .frame = std::move(frame),
                .pattern = std::move(*pattern),
                .useful = true,
            }
        );
    }

    auto coverage_arms = std::vector<PatternCoverageArm>();
    coverage_arms.reserve(plans.size());
    for (const auto& plan : plans) {
        auto alternatives = std::vector<std::optional<PatternID>>();
        const auto pattern = body_builder.pattern_copy(plan.pattern.pattern);
        if (const auto* disjunction = std::get_if<OrPattern>(&pattern.value)) {
            alternatives.reserve(disjunction->alternatives.size());
            for (const auto alternative : disjunction->alternatives) {
                alternatives.emplace_back(alternative);
            }
        } else {
            alternatives.emplace_back(plan.pattern.pattern);
        }
        coverage_arms.push_back(
            PatternCoverageArm {
                .alternatives = std::move(alternatives),
                .guarded = plan.source->guard.has_value(),
            }
        );
    }
    auto coverage = compute_pattern_coverage(
        draft(),
        body_builder.pattern_table(),
        subject_type,
        coverage_arms
    );
    if (!coverage.has_value()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::TypeMatchPattern,
            std::format("invalid match pattern coverage: {}", coverage.error())
        ));
    }
    if (!coverage->redundant_alternatives.empty()) {
        const auto redundant = coverage->redundant_alternatives.front();
        auto diagnostic_span = plans[redundant.arm].source->span;
        const auto* disjunction =
            std::get_if<ASTOrPattern>(&ast.pattern(plans[redundant.arm].source->pattern).value);
        if (disjunction != nullptr && redundant.alternative < disjunction->alternatives.size()) {
            diagnostic_span = ast.pattern(disjunction->alternatives[redundant.alternative]).span;
        }
        return std::unexpected(fail(
            diagnostic_span,
            DiagnosticCode::MatchDuplicateAlternative,
            "match or-pattern contains a repeated or subsumed alternative"
        ));
    }
    for (auto index = 0uz; index < plans.size(); ++index) {
        plans[index].useful = coverage->arm_usefulness[index];
        if (!plans[index].useful) {
            warn(
                ast.pattern(plans[index].source->pattern).span,
                DiagnosticCode::FlowUnreachableMatchArm,
                "match arm is unreachable because an earlier arm covers it"
            );
            for (auto iterator = plans[index].frame.names.begin();
                 iterator != plans[index].frame.names.end();
                 ++iterator) {
                iterator->second.unused_candidate.reset();
            }
        }
    }
    if (!coverage->exhaustive) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::MatchNonExhaustive,
            std::format(
                "match patterns do not cover every subject value; for example {}",
                coverage->missing_witness
            )
        ));
    }

    auto arms = std::vector<SemMatchArm>();
    auto normal = false;
    auto remaining = selection_reachable;
    for (auto& plan : plans) {
        const auto useful = remaining && plan.useful;
        [[maybe_unused]] const auto path = BodyReferencePathGuard(reference_path_reachable, useful);
        frames.push_back(std::move(plan.frame));
        [[maybe_unused]] const auto suspended =
            BodyFullExpressionSuspension(active_full_expression);
        reachable = true;
        auto guard_tree = std::optional<SemanticExpression>();
        auto body_reachable = true;
        auto guard_may_reject = false;
        if (plan.source->guard.has_value()) {
            const auto id = plan.source->guard->expression;
            auto guard = expression(id, draft().intern_builtin_type(BuiltinType::Bool));
            if (!guard.has_value()) {
                return std::unexpected(guard.error());
            }
            if (value_form) {
                collect_pending(pending, *guard);
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
        if (plan.pattern.irrefutable && !guard_may_reject) {
            remaining = false;
        }
        auto body = [&]() noexcept -> AnalysisResult<SemanticRegion> {
            [[maybe_unused]] const auto body_path =
                BodyReferencePathGuard(reference_path_reachable, body_reachable);
            return build_arm(
                plan.source->body,
                value_form,
                result_type,
                pending,
                expected.has_value() && allow_pointer_narrowing
            );
        }();
        if (!body.has_value()) {
            return std::unexpected(body.error());
        }
        normal = normal || (useful && body_reachable && reachable);
        arms.push_back(
            {plan.pattern.pattern,
             std::move(plan.pattern.bindings),
             std::move(guard_tree),
             std::move(*body),
             useful}
        );
        pop_frame();
    }
    reachable = normal;
    auto result = make_built(
        result_type.value_or(draft().intern_builtin_type(BuiltinType::Void)),
        SemMatch {UniqueIndirect(std::move(subject_tree)), subject_is_place, std::move(arms)},
        span,
        std::move(pending)
    );
    if (!value_form) {
        append_expression(result, span);
        return std::optional<BuiltExpression>();
    }
    return std::optional(normal ? std::move(result) : mark_noncompleting(std::move(result)));
}

auto BodyElaborator::match_expression(
    const ASTMatchForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    bool allow_pointer_narrowing
) noexcept -> AnalysisResult<BuiltExpression> {
    auto built = build_match(source, span, expected, true, allow_pointer_narrowing);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    if (!built->has_value()) {
        invariant_violation("value match did not produce a value");
    }
    return std::move(**built);
}

auto BodyElaborator::match_statement(const ASTMatchForm& source, Span span) noexcept
    -> AnalysisResult<void> {
    auto built = build_match(source, span, std::nullopt, false);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    return {};
}
