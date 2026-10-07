module carven:semantic.analysis.ownership.facts.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.ownership.context;
import :semantic.semir.callable;
import :semantic.semir.sequence;
import :semantic.semir.text;
import std;

namespace {

auto type_has_relationships(const TypeContents& contents) noexcept -> bool {
    return contents.contains_closure_owner
        || contents.contains_callable_view
        || contents.contains_storage_view;
}

auto is_scalar_write_type(const SemIRProgram& program, TypeID type) noexcept -> bool {
    const auto contents = program.type_contents(type);
    return std::holds_alternative<BuiltinTypeValue>(program.types().type(type).value)
        && contents.read_is_value_snapshot()
        && !contents.contains_storage_view;
}

auto observe_relation_demand(
    const SemIRProgram& program,
    const SemanticExpression& expression,
    OwnershipRelationDemand& demand
) noexcept -> void {
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
                    if constexpr (std::same_as<Intrinsic, SequenceIntrinsicOperation>) {
                        return sequence_intrinsic_contract(intrinsic.intrinsic).receiver_access
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
                    const auto scalar = is_scalar_write_type(program, operand.type.resolved());
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
                    !is_scalar_write_type(program, expression.type.resolved());
            }
        } else if constexpr (std::same_as<Operation, SemCall>) {
            auto target = operation.target;
            if (!target) {
                target = callable_identity(program, operation.callee->type.resolved());
            }
            const auto external =
                !target || !program.declarations().body_for_callable(*target).has_value();
            demand.observes_relations |=
                !target || (external && !is_scalar_write_type(program, expression.type.resolved()));
            for (const auto& argument : operation.arguments) {
                demand.writes_storage |=
                    argument.access == AccessMode::Write || argument.access == AccessMode::Take;
                demand.observes_relations |= argument.access == AccessMode::Take
                    || (external
                        && (argument.access != AccessMode::Read
                            || !is_scalar_write_type(
                                program,
                                argument.expression.type.resolved()
                            )));
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
    const SemIRBody& body,
    const SemIRProgram& program,
    OwnershipRecursionBuilder& recursion
) noexcept -> OwnershipBodyFacts {
    auto facts = OwnershipBodyFacts {};
    // Captures read through closure-holder relationship rows.
    facts.relation_demand.observes_relations = !body.inputs().captures.empty();
    const auto add = [&](TypeID type, ProgramOriginID origin, LifetimeRegionID lifetime) noexcept {
        const auto index = facts.locals.size();
        facts.locals.push_back({type, origin, lifetime});
        facts.lifetime_objects[lifetime].push_back(index);
        return index;
    };
    for (const auto [id, binding] : body.bindings()) {
        if (id.index() != facts.locals.size()) {
            invariant_violation("local object positions disagree with bindings");
        }
        add(binding.type, binding.origin, binding.lifetime);
        const auto contents = program.type_contents(binding.type);
        const auto relationships = type_has_relationships(contents);
        facts.relation_demand.produces_relationships |= relationships;
        facts.relation_demand.observes_relations |= contents.contains_native_value
            || (relationships
                && (std::holds_alternative<ParameterBindingStorage>(binding.storage)
                    || std::holds_alternative<CaptureBindingStorage>(binding.storage)));
    }
    recursion.begin_body(body.id());
    const auto observe_expression = [&](const SemanticExpression& expression) noexcept {
        recursion.observe(expression);
        const auto contents = program.type_contents(expression.type.resolved());
        facts.relation_demand.produces_relationships |= type_has_relationships(contents);
        facts.relation_demand.observes_relations |= contents.contains_native_value;
        observe_relation_demand(program, expression, facts.relation_demand);
        if (!expression.selects_storage()
            && (contents.contains_closure_owner
                || contents.contains_callable_view
                || contents.contains_storage_owner)) {
            facts.temporaries.emplace(
                std::addressof(expression),
                add(expression.type.resolved(), expression.origin, expression.lifetime)
            );
        }
        const auto* attempt = std::get_if<SemTry>(&expression.value);
        if (attempt == nullptr) {
            return;
        }
        const auto& failures =
            program.failure_sets().failure_set(attempt->protected_failures.resolved());
        for (const auto& arm : attempt->arms) {
            auto accepted = std::flat_map<TypeID, OwnershipCatchAcceptance>();
            for (const auto type : failures.members) {
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
                auto complete = patterns_exhaustive(program, body.pattern_table(), type, patterns);
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
                        if (const auto* assignment = std::get_if<SemAssign>(&statement.value)) {
                            facts.relation_demand.writes_storage = true;
                            facts.relation_demand.observes_relations |=
                                !is_scalar_write_type(program, assignment->target.type.resolved());
                        }
                    }}
    );
    return facts;
}

} // namespace

auto prepare_ownership_analysis(const SemIRProgram& program) noexcept -> OwnershipPreparation {
    auto result = OwnershipPreparation {};
    auto recursion = OwnershipRecursionBuilder(program);
    for (const auto [id, body] : program.bodies().entries()) {
        result.body_facts.emplace(id, prepare_ownership_body_facts(body, program, recursion));
    }
    result.recursion_components = std::move(recursion).finish(result.body_facts);
    return result;
}
