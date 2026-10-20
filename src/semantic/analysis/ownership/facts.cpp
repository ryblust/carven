module carven:semantic.analysis.ownership.facts.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
import :semantic.semir.text;
import :semantic.semir.vector;
import std;

namespace {

auto type_has_relationships(const TypeContents& contents) noexcept -> bool {
    return contents.contains_parametric_value
        || contents.contains_operation_owner
        || contents.contains_closure_owner
        || contents.contains_callable_view
        || contents.contains_storage_view;
}

auto is_scalar_write_type(const AnalysisModelView& model, ConstructionTypeRef type) noexcept
    -> bool {
    const auto contents = model.contents(type);
    return std::holds_alternative<BuiltinTypeValue>(model.shape(type))
        && contents.read_is_value_snapshot()
        && !contents.contains_storage_view;
}

auto observe_relation_demand(
    const AnalysisModelView& model,
    const SemanticExpression& expression,
    OwnershipRelationDemand& demand
) noexcept -> void {
    const auto& program = model.program;
    expression.value.visit([&](const auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemTake>) {
            demand.observes_relations = true;
            demand.writes_storage = true;
        } else if constexpr (std::same_as<Operation, SemMatch>) {
            demand.observes_relations = true;
        } else if constexpr (std::same_as<Operation, SemFormat>) {
            demand.observes_relations |= operation.receiver.has_value();
            demand.writes_storage |= operation.receiver.has_value();
        } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
            const auto writes =
                operation.operation.visit([](const auto& intrinsic) static noexcept {
                    using Intrinsic = std::remove_cvref_t<decltype(intrinsic)>;
                    if constexpr (std::same_as<Intrinsic, VectorIntrinsicOperation>) {
                        return vector_intrinsic_contract(intrinsic.intrinsic).receiver_access
                            == AccessMode::Write;
                    } else if constexpr (std::same_as<Intrinsic, TextIntrinsic>) {
                        return text_intrinsic_writes(intrinsic);
                    } else {
                        return false;
                    }
                });
            demand.observes_relations |= writes;
            demand.writes_storage |= writes;
        } else if constexpr (std::same_as<Operation, SemCpp>
                             || std::same_as<Operation, SemCppCall>) {
            visit_cpp_operands(
                operation,
                [&](AccessMode access, const SemanticExpression& operand) noexcept {
                    const auto scalar = is_scalar_write_type(model, operand.type.construction());
                    demand.writes_storage |=
                        access == AccessMode::Write || access == AccessMode::Take;
                    if constexpr (std::same_as<Operation, SemCppCall>) {
                        demand.observes_relations |= access != AccessMode::Read || !scalar;
                    } else {
                        demand.observes_relations |=
                            access == AccessMode::Take || (access == AccessMode::Write && !scalar);
                    }
                }
            );
            if constexpr (std::same_as<Operation, SemCppCall>) {
                demand.observes_relations |=
                    !is_scalar_write_type(model, expression.type.construction());
            }
        } else if constexpr (std::same_as<Operation, SemCall>) {
            auto target = operation.target;
            if (!target) {
                target = model.callable_identity(operation.callee->type.construction());
            }
            const auto external =
                !target || !program.declarations().body_for_callable(*target).has_value();
            demand.observes_relations |= !target
                || (external && !is_scalar_write_type(model, expression.type.construction()));
            for (const auto& argument : operation.arguments) {
                demand.writes_storage |=
                    argument.access == AccessMode::Write || argument.access == AccessMode::Take;
                demand.observes_relations |= argument.access == AccessMode::Take
                    || (external
                        && (argument.access != AccessMode::Read
                            || !is_scalar_write_type(
                                model,
                                argument.expression.type.construction()
                            )));
            }
        } else if constexpr (std::same_as<Operation, SemGenericCall>) {
            demand.observes_relations = true;
            for (const auto& argument : operation.arguments) {
                demand.writes_storage |= argument.access != AccessMode::Read;
            }
        } else if constexpr (std::same_as<Operation, SemClosure>) {
            demand.writes_storage |=
                std::ranges::any_of(operation.captures, [](const auto& capture) static noexcept {
                    return capture.mode == CaptureMode::Write;
                });
        }
    });
}

auto prepare_ownership_body_facts(
    const AnalysisBodyView& body,
    const AnalysisModelView& model,
    OwnershipRecursionBuilder& recursion
) noexcept -> OwnershipBodyFacts {
    const auto& program = model.program;
    auto facts = OwnershipBodyFacts {
        .relation_demand =
            OwnershipRelationDemand {
                .observes_relations = false,
                .produces_relationships = false,
                .writes_storage = false
            },
        .locals = {},
        .retained_bindings = {},
        .temporaries = {},
        .lifetime_objects = {},
        .catches = {}
    };
    // Captures read through closure-holder relationship rows.
    facts.relation_demand.observes_relations = !body.inputs().captures.empty();
    const auto add =
        [&](ConstructionTypeRef type, ProgramOriginID origin, LifetimeRegionID lifetime) noexcept {
            const auto index = facts.locals.size();
            facts.locals.push_back(
                OwnershipLocalObject {.type = type, .origin = origin, .lifetime = lifetime}
            );
            facts.lifetime_objects[lifetime].push_back(index);
            return index;
        };
    for (const auto& [id, binding] : body.bindings()) {
        if (id.index() != facts.locals.size()) {
            invariant_violation("local object positions disagree with bindings");
        }
        add(binding.type, binding.origin, binding.lifetime);
        const auto contents = model.contents(binding.type);
        const auto relationships = type_has_relationships(contents);
        facts.relation_demand.produces_relationships |= relationships;
        facts.relation_demand.observes_relations |= contents.contains_native_value
            || contents.contains_parametric_value
            || (relationships
                && (std::holds_alternative<ParameterBindingStorage>(binding.storage)
                    || std::holds_alternative<CaptureBindingStorage>(binding.storage)));
    }
    recursion.begin_body(body.id());
    const auto observe_expression = [&](const SemanticExpression& expression) noexcept {
        recursion.observe(expression);
        const auto contents = model.contents(expression.type.construction());
        facts.relation_demand.produces_relationships |= type_has_relationships(contents);
        facts.relation_demand.observes_relations |=
            contents.contains_native_value || contents.contains_parametric_value;
        observe_relation_demand(model, expression, facts.relation_demand);
        if (!expression.selects_storage()
            && (contents.contains_parametric_value
                || contents.contains_operation_owner
                || contents.contains_closure_owner
                || contents.contains_callable_view
                || contents.contains_storage_owner)) {
            facts.temporaries.emplace(
                std::addressof(expression),
                add(expression.type.construction(), expression.origin, expression.lifetime)
            );
        }
        const auto* attempt = std::get_if<SemTry>(&expression.value);
        if (attempt == nullptr) {
            return;
        }
        auto failures = model.failure_known_members(attempt->protected_failures.reference());
        for (const auto& arm : attempt->arms) {
            for (const auto& alternative : arm.alternatives) {
                if (const auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                    const auto type = typed->type.resolved();
                    if (!std::ranges::contains(failures, type)
                        && model.failure_may_contain(
                            attempt->protected_failures.reference(),
                            type
                        )) {
                        failures.push_back(type);
                    }
                }
            }
        }
        for (const auto& arm : attempt->arms) {
            auto accepted = std::flat_map<TypeID, OwnershipCatchAcceptance>();
            for (const auto type : failures) {
                auto alternatives = std::vector<std::optional<PatternID>>();
                for (const auto& alternative : arm.alternatives) {
                    if (!alternative.reachable) {
                        continue;
                    }
                    if (const auto* typed =
                            std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                        if (typed->type.resolved() == type) {
                            alternatives.push_back(typed->inner);
                        }
                    } else {
                        alternatives.push_back(std::nullopt);
                    }
                }
                if (alternatives.empty()) {
                    continue;
                }
                const auto patterns = std::array {
                    PatternCoverageArm {.alternatives = alternatives, .guarded = false}
                };
                auto complete = body.concrete()
                    ? patterns_exhaustive(program, body.pattern_table(), type, patterns)
                    : patterns_exhaustive(program, body.elaborated_pattern_table(), type, patterns);
                if (!complete.has_value()) {
                    invariant_violation(complete.error());
                }
                accepted.emplace(
                    type,
                    OwnershipCatchAcceptance {
                        .alternatives = std::move(alternatives),
                        .exhaustive = *complete
                    }
                );
            }
            facts.catches.emplace(std::addressof(arm), std::move(accepted));
        }
    };
    visit_semantic_nodes(
        body.region(),
        Overloaded {observe_expression, [&](const SemanticStatement& statement) noexcept {
                        if (const auto* binding = std::get_if<SemStaticBinding>(&statement.value)) {
                            facts.retained_bindings.insert(binding->binding);
                        }
                        if (const auto* assignment = std::get_if<SemAssign>(&statement.value)) {
                            facts.relation_demand.writes_storage = true;
                            facts.relation_demand.observes_relations |= !is_scalar_write_type(
                                model,
                                assignment->target.type.construction()
                            );
                        }
                    }}
    );
    return facts;
}

} // namespace

auto prepare_ownership_analysis(const AnalysisModelView& model) noexcept -> OwnershipPreparation {
    auto result = OwnershipPreparation {.body_facts = {}, .recursion_components = {}};
    auto recursion = OwnershipRecursionBuilder(model);
    for (const auto& [id, body] : model.bodies()) {
        result.body_facts.emplace(id, prepare_ownership_body_facts(body, model, recursion));
    }
    result.recursion_components = std::move(recursion).finish(result.body_facts);
    return result;
}
