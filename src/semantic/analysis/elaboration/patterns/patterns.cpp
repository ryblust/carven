module carven:semantic.analysis.elaboration.patterns.impl;

import :frontend.ast.decl;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.type;
import :frontend.literal;
import :semantic.analysis.coverage;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.patterns;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto analyze_match(
    ModuleAnalysis& module_analysis,
    HIRTypeID subject_type,
    std::span<const HIRMatchArm> arms,
    Span match_span
) noexcept -> bool {
    const auto& builder = module_analysis.builder();
    auto coverage =
        compute_pattern_coverage(builder, module_analysis.declarations(), subject_type, arms);
    if (!coverage.has_value()) {
        invariant_violation("pattern coverage could not be computed after type analysis");
    }
    for (auto index = 0uz; index < arms.size(); ++index) {
        if (!coverage->arm_usefulness[index]) {
            module_analysis.emit(
                builder.provenance().origin(builder.pattern(arms[index].pattern).origin).span,
                "match arm is unreachable because previous unguarded arms cover it",
                DiagnosticCode::FlowUnreachableMatchArm
            );
        }
    }
    if (!coverage->exhaustive) {
        module_analysis.emit(
            match_span,
            std::format("match is not exhaustive; missing witness {}", coverage->missing_witness),
            DiagnosticCode::MatchNonExhaustive
        );
    }
    return coverage->exhaustive;
}

auto analyze_catch_pattern(
    ModuleAnalysis& module_analysis,
    std::span<const HIRCatchPatternAlternative> alternatives
) noexcept -> void {
    const auto& builder = module_analysis.builder();
    const auto emit_redundant = [&](ProgramOriginID origin) noexcept {
        module_analysis.emit(
            builder.provenance().origin(origin).span,
            "catch pattern contains a repeated or subsumed alternative",
            DiagnosticCode::MatchDuplicateAlternative
        );
    };

    const auto wildcard =
        std::ranges::find_if(alternatives, [](const auto& alternative) static noexcept {
            return !alternative.type.has_value();
        });
    if (wildcard != alternatives.end()) {
        for (const auto& alternative : alternatives) {
            if (&alternative != &*wildcard) {
                emit_redundant(alternative.origin);
            }
        }
        return;
    }

    auto analyzed = std::vector(alternatives.size(), false);
    for (auto source_index = 0uz; source_index < alternatives.size(); ++source_index) {
        if (analyzed[source_index]) {
            continue;
        }
        const auto type = *alternatives[source_index].type;
        auto source_indices = std::vector<std::size_t>();
        auto patterns = std::vector<std::optional<HIRPatternID>>();
        for (auto index = source_index; index < alternatives.size(); ++index) {
            if (alternatives[index].type == type) {
                analyzed[index] = true;
                source_indices.push_back(index);
                patterns.push_back(alternatives[index].inner);
            }
        }
        if (source_indices.size() == 1) {
            continue;
        }
        const auto query = PatternCoverageArm {
            .alternatives = std::move(patterns),
            .guarded = false,
        };
        const auto coverage = compute_pattern_coverage(
            builder,
            module_analysis.declarations(),
            type,
            std::span(&query, 1)
        );
        if (!coverage.has_value()) {
            invariant_violation("catch pattern coverage could not be computed after type analysis");
        }
        for (const auto& redundant : coverage->redundant_alternatives) {
            if (redundant.alternative >= source_indices.size()) {
                invariant_violation("catch coverage alternative is out of range");
            }
            emit_redundant(alternatives[source_indices[redundant.alternative]].origin);
        }
    }
}

auto pattern_alternatives(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    std::span<const PatternAlternativeSubject> subjects
) noexcept -> std::vector<std::optional<HIRPatternID>> {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    struct PatternBinding final {
        SymbolID symbol;
        HIRTypeID type;
    };
    using Bindings = std::flat_map<std::string, PatternBinding, std::less<>>;
    auto bindings = Bindings();
    const auto lower = [&](this const auto& self,
                           ASTPatternID pattern_id,
                           HIRTypeID expected,
                           Bindings& local,
                           const Bindings* canonical) noexcept -> HIRPatternID {
        const auto& source_pattern = ast.pattern(pattern_id);
        return std::visit(
            Overloaded {
                [&](const ASTWildcardPattern&) noexcept -> HIRPatternID {
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIRWildcardPattern {},
                    });
                },
                [&](const ASTLiteral& literal) noexcept -> HIRPatternID {
                    const auto inferred = literal_type(module_analysis, literal);
                    const auto numeric = as_numeric_literal(literal);
                    const auto contextual_numeric = numeric.has_value()
                        && numeric_suffix(*numeric) == NumericSuffix::None
                        && is_numeric(module_analysis, expected)
                        && is_numeric(module_analysis, inferred)
                        && is_integer(module_analysis, expected)
                            == is_integer(module_analysis, inferred);
                    const auto resolved = contextual_numeric ? expected : inferred;
                    if (!compatible(module_analysis, expected, resolved)) {
                        module_analysis.emit(
                            source_pattern.span,
                            "match literal is incompatible with its subject",
                            DiagnosticCode::TypeMatchPattern
                        );
                    }
                    const auto fact = literal_fact(module_analysis, literal, resolved);
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIRLiteralPattern {
                            .literal = fact.value,
                            .type = resolved,
                        },
                    });
                },
                [&](const ASTNegativeNumberPattern& negative) noexcept -> HIRPatternID {
                    const auto inferred =
                        numeric_literal_type(module_analysis, negative.number_span, negative.value);
                    const auto resolved = numeric_suffix(negative.value) == NumericSuffix::None
                            && is_numeric(module_analysis, expected)
                            && is_numeric(module_analysis, inferred)
                            && is_integer(module_analysis, expected)
                                == is_integer(module_analysis, inferred)
                        ? expected
                        : inferred;
                    if (!compatible(module_analysis, expected, resolved)) {
                        module_analysis.emit(
                            source_pattern.span,
                            "match literal is incompatible with its subject",
                            DiagnosticCode::TypeMatchPattern
                        );
                    }
                    const auto fact = negative_literal_fact(
                        module_analysis,
                        negative.number_span,
                        negative.value,
                        resolved
                    );
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIRLiteralPattern {
                            .literal = fact.value,
                            .type = resolved,
                        },
                    });
                },
                [&](const ASTBindingPattern& binding) noexcept -> HIRPatternID {
                    const auto spelling_value = module_analysis.spelling(binding.name_span);
                    const auto key = std::string(spelling_value);
                    if (const auto found = local.find(key); found != local.end()) {
                        module_analysis.emit(
                            binding.name_span,
                            "pattern binds the same name more than once",
                            DiagnosticCode::NameDuplicateLocal
                        );
                        return builder.append_pattern({
                            .origin = module_analysis.origin(source_pattern.span),
                            .value = HIRBindingPattern {
                                .target = {.symbol = found->second.symbol},
                                .type = expected,
                            },
                        });
                    }
                    const auto name = builder.intern_string(spelling_value);
                    const auto symbol = [&]() noexcept -> SymbolID {
                        if (canonical == nullptr) {
                            const auto created = module_analysis.append_symbol({
                                .name = name,
                                .module_id = std::nullopt,
                                .role = SemanticSymbolRole::Local,
                                .parent = std::nullopt,
                            });
                            set_symbol_type(module_analysis, created, expected);
                            builder
                                .define_symbol_binding(created, SemanticBindingRole::Owner, false);
                            builder.record_symbol_lint_candidate(
                                created,
                                module_analysis.origin(binding.name_span)
                            );
                            return created;
                        }
                        const auto found = canonical->find(key);
                        if (found != canonical->end()) {
                            if (!compatible(module_analysis, found->second.type, expected)) {
                                module_analysis.emit(
                                    binding.name_span,
                                    "or-pattern binding has a different type",
                                    DiagnosticCode::MatchBindingMismatch
                                );
                            }
                            return found->second.symbol;
                        } else {
                            module_analysis.emit(
                                binding.name_span,
                                "or-pattern alternatives must bind the same names",
                                DiagnosticCode::MatchBindingMismatch
                            );
                            const auto created = module_analysis.append_symbol({
                                .name = name,
                                .module_id = std::nullopt,
                                .role = SemanticSymbolRole::Local,
                                .parent = std::nullopt,
                            });
                            set_symbol_type(module_analysis, created, expected);
                            builder
                                .define_symbol_binding(created, SemanticBindingRole::Owner, false);
                            return created;
                        }
                    }();
                    local.emplace(
                        key,
                        PatternBinding {
                            .symbol = symbol,
                            .type = expected,
                        }
                    );
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIRBindingPattern {
                            .target = {.symbol = symbol},
                            .type = expected,
                        },
                    });
                },
                [&](const ASTConstraintPattern& constraint) noexcept -> HIRPatternID {
                    const auto constrained =
                        constraint_type(module_analysis, scopes, control, constraint.operand);
                    if (!compatible(module_analysis, expected, constrained)
                        && !is_opaque_or_error(module_analysis, expected)) {
                        module_analysis.emit(
                            source_pattern.span,
                            "type constraint is incompatible with the subject",
                            DiagnosticCode::TypeMatchConstraint
                        );
                    }
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIRTypeConstraintPattern {.type = constrained},
                    });
                },
                [&](const ASTCasePattern& case_pattern) noexcept -> HIRPatternID {
                    auto enum_type = expected;
                    const auto case_resolution = [&]() noexcept -> LookupResult<EnumCaseID> {
                        if (std::holds_alternative<ASTContextualCaseQualifier>(
                                case_pattern.qualifier
                            )) {
                            const auto member_resolution = resolve_enum_case(
                                module_analysis,
                                expected,
                                module_analysis.spelling(case_pattern.name_span),
                                case_pattern.name_span
                            );
                            if (!member_resolution.has_value()) {
                                return std::unexpected(member_resolution.error());
                            }
                            return member_resolution.value().id;
                        }

                        const auto& qualifier =
                            std::get<ASTQualifiedCaseQualifier>(case_pattern.qualifier);
                        auto components = qualifier.components;
                        components.push_back(case_pattern.name_span);
                        const auto resolution = resolve_qualified_value(
                            module_analysis,
                            scopes,
                            ASTQualifiedName {
                                .span = qualifier.span,
                                .components = std::move(components),
                            }
                        );
                        if (resolution.has_value()) {
                            const auto* catalog_symbol =
                                module_analysis.catalog().symbol(resolution.value());
                            const auto* enum_case = catalog_symbol == nullptr
                                ? nullptr
                                : std::get_if<CatalogEnumCaseForm>(&catalog_symbol->form);
                            if (enum_case != nullptr) {
                                enum_type = builder.intern_type({
                                    .value = HIREnumTypeValue {
                                        .enumeration = enum_case->owner,
                                    },
                                });
                            }
                        }
                        if (!resolution.has_value()) {
                            return std::unexpected(resolution.error());
                        }
                        const auto* catalog_symbol =
                            module_analysis.catalog().symbol(resolution.value());
                        const auto* enum_case = catalog_symbol == nullptr
                            ? nullptr
                            : std::get_if<CatalogEnumCaseForm>(&catalog_symbol->form);
                        return enum_case == nullptr
                            ? std::unexpected(LookupError::Missing)
                            : LookupResult<EnumCaseID>(enum_case->enum_case);
                    }();
                    const auto diagnosed_failure = !case_resolution.has_value()
                        && case_resolution.error() == LookupError::Diagnosed;
                    const auto case_id = case_resolution.has_value()
                        ? std::optional(case_resolution.value())
                        : std::nullopt;
                    const auto compatible_case =
                        case_id.has_value() && compatible(module_analysis, enum_type, expected);
                    if (!diagnosed_failure && !compatible_case) {
                        module_analysis.emit(
                            case_pattern.name_span,
                            "enum case pattern does not belong to the subject type",
                            DiagnosticCode::TypeMatchPattern
                        );
                    }
                    const auto payload_types = case_id.has_value()
                        ? module_analysis.declarations().enum_case(*case_id).payload_types
                        : std::span<const HIRTypeID>();
                    const auto payload_size = case_pattern.payload.has_value()
                        ? case_pattern.payload->patterns.size()
                        : 0uz;
                    const auto resolved_member = case_id.has_value();
                    const auto valid_payload_shape = resolved_member
                        && payload_size == payload_types.size()
                        && (!payload_types.empty() || !case_pattern.payload.has_value());
                    if (resolved_member) {
                        if (payload_size != payload_types.size()) {
                            module_analysis.emit(
                                source_pattern.span,
                                "enum case pattern payload arity does not match",
                                DiagnosticCode::TypeEnumCaseArity
                            );
                        }
                        if (payload_types.empty() && case_pattern.payload.has_value()) {
                            module_analysis.emit(
                                source_pattern.span,
                                "nullary enum case pattern must not use parentheses",
                                DiagnosticCode::TypeEnumCaseArity
                            );
                        }
                    }
                    auto payload = std::vector<HIRPatternID>();
                    for (auto index = 0uz; index < payload_size; ++index) {
                        payload.push_back(self(
                            case_pattern.payload->patterns[index],
                            index < payload_types.size()
                                ? payload_types[index]
                                : error_type(module_analysis, source_pattern.span),
                            local,
                            canonical
                        ));
                    }
                    if (!compatible_case || !valid_payload_shape) {
                        return builder.append_pattern({
                            .origin = module_analysis.origin(source_pattern.span),
                            .value = HIRWildcardPattern {},
                        });
                    }
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIRCasePattern {
                            .enum_case = *case_id,
                            .payload = std::move(payload),
                        },
                    });
                },
                [&](const ASTOrPattern& alternatives) noexcept -> HIRPatternID {
                    auto lowered = std::vector<HIRPatternID>();
                    auto canonical_bindings = Bindings();
                    for (auto index = 0uz; index < alternatives.alternatives.size(); ++index) {
                        auto alternative_bindings = Bindings();
                        lowered.push_back(self(
                            alternatives.alternatives[index],
                            expected,
                            alternative_bindings,
                            index == 0 ? canonical : &canonical_bindings
                        ));
                        if (index == 0) {
                            canonical_bindings = alternative_bindings;
                        } else {
                            for (const auto& [name, binding] : canonical_bindings) {
                                if (!alternative_bindings.contains(name)) {
                                    module_analysis.emit(
                                        source_pattern.span,
                                        "or-pattern alternatives must bind the same names",
                                        DiagnosticCode::MatchBindingMismatch
                                    );
                                }
                            }
                        }
                    }
                    for (const auto& [name, binding] : canonical_bindings) {
                        local.emplace(name, binding);
                    }
                    auto query = PatternCoverageArm {
                        .alternatives = {},
                        .guarded = false,
                    };
                    query.alternatives.reserve(lowered.size());
                    for (const auto alternative : lowered) {
                        query.alternatives.push_back(alternative);
                    }
                    const auto coverage = compute_pattern_coverage(
                        builder,
                        module_analysis.declarations(),
                        expected,
                        std::span(&query, 1)
                    );
                    if (!coverage.has_value()) {
                        invariant_violation(
                            "or-pattern coverage could not be computed after type analysis"
                        );
                    }
                    for (const auto& redundant : coverage->redundant_alternatives) {
                        if (redundant.alternative >= lowered.size()) {
                            invariant_violation("or-pattern coverage alternative is out of range");
                        }
                        module_analysis.emit(
                            builder.provenance()
                                .origin(builder.pattern(lowered[redundant.alternative]).origin)
                                .span,
                            "or-pattern contains a repeated or subsumed alternative",
                            DiagnosticCode::MatchDuplicateAlternative
                        );
                    }
                    return builder.append_pattern({
                        .origin = module_analysis.origin(source_pattern.span),
                        .value = HIROrPattern {
                            .type = expected,
                            .alternatives = std::move(lowered),
                        },
                    });
                },
            },
            source_pattern.value
        );
    };
    auto result = std::vector<std::optional<HIRPatternID>>();
    result.reserve(subjects.size());
    for (auto index = 0uz; index < subjects.size(); ++index) {
        if (const auto* wildcard = std::get_if<WildcardPatternSubject>(&subjects[index])) {
            if (index == 0) {
                bindings.clear();
            } else if (!bindings.empty()) {
                module_analysis.emit(
                    wildcard->span,
                    "pattern alternatives must bind the same names",
                    DiagnosticCode::MatchBindingMismatch
                );
            }
            result.push_back(std::nullopt);
            continue;
        }
        const auto& subject = std::get<PatternSubject>(subjects[index]);
        auto alternative_bindings = Bindings();
        result.push_back(lower(
            subject.pattern,
            subject.type,
            alternative_bindings,
            index == 0 ? nullptr : &bindings
        ));
        if (index == 0) {
            bindings = alternative_bindings;
            continue;
        }
        for (const auto& [name, binding] : bindings) {
            if (!alternative_bindings.contains(name)) {
                module_analysis.emit(
                    ast.pattern(subject.pattern).span,
                    "pattern alternatives must bind the same names",
                    DiagnosticCode::MatchBindingMismatch
                );
            }
        }
    }
    for (const auto& [name, binding] : bindings) {
        if (!scopes.bind(name, binding.symbol)) {
            invariant_violation("canonical pattern binding was already published in its scope");
        }
    }
    return result;
}

auto build_pattern(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTPatternID id,
    HIRTypeID subject_type
) noexcept -> HIRPatternID {
    auto lowered = pattern_alternatives(
        module_analysis,
        scopes,
        control,
        std::array<PatternAlternativeSubject, 1> {
            PatternSubject {
                .pattern = id,
                .type = subject_type,
            },
        }
    );
    if (lowered.size() != 1 || !lowered.front().has_value()) {
        invariant_violation("single pattern lowering did not produce exactly one pattern");
    }
    return *lowered.front();
}

auto constraint_type(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstraintOperand& operand
) noexcept -> HIRTypeID {
    auto& builder = module_analysis.builder();
    return std::visit(
        Overloaded {
            [&](const ASTQualifiedName& qualified) noexcept -> HIRTypeID {
                auto components = std::vector<ASTTypeNameComponent>();
                components.reserve(qualified.components.size());
                for (const auto component : qualified.components) {
                    components.push_back({
                        .name_span = component,
                    });
                }
                return construction_type(
                    module_analysis,
                    scopes,
                    control,
                    {
                        .span = qualified.span,
                        .value = ASTNamedType {.components = std::move(components)},
                    }
                );
            },
            [&](const ASTArrayType& array) noexcept -> HIRTypeID {
                const auto extent = array_extent(module_analysis, scopes, control, array.extent);
                if (!extent.has_value()) {
                    return error_type(module_analysis, operand.span);
                }
                return builder.intern_type({
                    .value = HIRArrayTypeValue {
                        .element_type_id =
                            build_type(module_analysis, scopes, control, array.element_type),
                        .extent = *extent,
                    },
                });
            },
        },
        operand.value
    );
}
