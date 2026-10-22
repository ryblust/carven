module carven:semantic.analysis.program.solve.impl;

import :semantic.analysis.body.resolve;
import :semantic.analysis.program;
import :semantic.semir.children;
import :semantic.semir.callable;
import :semantic.semir.evaluation;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto generic_implementation_dependencies(
    const GenericAnalysisSnapshot& generics,
    GenericDeclarationID definition,
    const FailureSetStore& failures
) noexcept -> GenericImplementationDependencies {
    auto types = std::flat_set<TypeID>();
    auto callables = std::flat_set<CallableID>();
    auto definitions = std::flat_set<GenericDeclarationID>();
    auto visited = std::flat_set<ConstructionTypeRef>();
    const auto collect_failures = [&](ConstructionFailureRef reference) noexcept {
        if (const auto* concrete = std::get_if<FailureSetID>(&reference)) {
            types.insert_range(failures.failure_set(*concrete).members);
        }
    };
    const auto collect_type = [&](this const auto& self,
                                  ConstructionTypeRef type) noexcept -> void {
        if (!visited.insert(type).second) {
            return;
        }
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            types.insert(*concrete);
            return;
        }
        const auto* generic = std::get_if<GenericTypeID>(&type);
        if (!generic) {
            invariant_violation("generic dependency publication contains an unresolved type term");
        }
        const auto& shape = generics.generic_type(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&shape)) {
            self(*concrete);
        } else if (const auto* application = std::get_if<GenericNominalApplication>(&shape)) {
            definitions.insert(application->definition);
            for (const auto& argument : application->arguments) {
                visit_generic_argument_failures(argument, collect_failures);
            }
        } else if (const auto* closure = std::get_if<GenericClosureType>(&shape)) {
            definitions.insert(closure->definition);
            for (const auto& argument : closure->arguments) {
                visit_generic_argument_failures(argument, collect_failures);
            }
        } else if (const auto* operation = std::get_if<GenericOperationType>(&shape)) {
            collect_failures(operation->failures);
        } else if (const auto* view = std::get_if<GenericCallableViewType>(&shape)) {
            collect_failures(view->failures);
        }
        visit_generic_type_children(shape, self);
    };
    generics.definition(definition).visit([&](const auto& source) noexcept {
        using Source = std::remove_cvref_t<decltype(source)>;
        if constexpr (std::same_as<Source, GenericCallableDefinition>) {
            for (const auto& parameter : source.signature.parameters) {
                collect_type(parameter.type);
            }
            collect_type(source.signature.result);
            collect_failures(source.signature.failures);
        } else if constexpr (std::same_as<Source, GenericRecordDefinition>) {
            for (const auto& field : source.fields) {
                collect_type(field.type);
            }
        } else {
            for (const auto& item : source.cases) {
                for (const auto type : item.payload_types) {
                    collect_type(type);
                }
            }
        }
    });
    if (const auto found = generics.bodies.find(definition); found != generics.bodies.end()) {
        const auto& body = *found->second;
        for (const auto binding : body.bindings.entries()) {
            collect_type(binding.value.type);
        }
        for (const auto pattern : body.patterns.entries()) {
            collect_type(pattern.value.type);
            if (const auto* constraint =
                    std::get_if<ElaboratedTypeConstraintPattern>(&pattern.value.value)) {
                collect_type(constraint->type);
            }
        }
        const auto observe = Overloaded {
            [&](const SemanticExpression& expression) noexcept {
                collect_type(expression.type.construction());
                collect_failures(expression.failures.reference());
                if (const auto* call = std::get_if<SemGenericCall>(&expression.value)) {
                    definitions.insert(call->definition);
                    for (const auto& argument : call->environment) {
                        visit_generic_argument_types(argument, collect_type);
                        visit_generic_argument_failures(argument, collect_failures);
                    }
                } else if (const auto* call = std::get_if<SemCall>(&expression.value)) {
                    if (call->target) {
                        call->target->visit([&](const auto target) noexcept {
                            if constexpr (std::same_as<
                                              std::remove_cvref_t<decltype(target)>,
                                              CallableID>) {
                                callables.insert(target);
                            } else {
                                collect_type(ConstructionTypeRef(target));
                            }
                        });
                    }
                } else if (const auto* cold = std::get_if<SemColdCall>(&expression.value)) {
                    callables.insert(cold->target);
                } else if (const auto* callable = std::get_if<SemCallable>(&expression.value)) {
                    callables.insert(callable->callable);
                }
            },
            [&](const SemanticStatement& statement) noexcept {
                if (const auto* failure = std::get_if<SemThrow>(&statement.value)) {
                    types.insert(failure->failure_type);
                }
            },
        };
        visit_semantic_nodes(body.region, observe);
        if (body.residual) {
            visit_semantic_nodes(*body.residual, observe);
        }
    }
    return {
        .types = {types.begin(), types.end()},
        .callables = {callables.begin(), callables.end()},
        .definitions = {definitions.begin(), definitions.end()},
    };
}

} // namespace

auto ProgramDraft::solve_call_stops(
    const CanonicalTypeStore& types,
    const TypeResolution& resolved_types,
    const DeclarationStore& declarations,
    const GenericAnalysisSnapshot& generics
) const noexcept -> std::vector<CallStopEffect> {
    const auto callable_count =
        static_cast<std::size_t>(std::ranges::distance(declarations.callables()));
    const auto count = callable_count + generics.definitions.size();
    auto stops = std::vector<CallStopEffect>(count, CallStopEffect::None);
    auto callers = std::vector<std::vector<std::size_t>>(count);
    auto pending = std::vector<std::size_t>();
    const auto generic_ordinal = [&](GenericDeclarationID definition) noexcept {
        static_cast<void>(generics.definition(definition));
        return callable_count + definition.index();
    };
    const auto mark = [&](std::size_t ordinal) noexcept {
        if (stops[ordinal] == CallStopEffect::None) {
            stops[ordinal] = CallStopEffect::MayStop;
            pending.push_back(ordinal);
        }
    };
    const auto scan = [&](std::size_t ordinal, const StructuredBodyDraft& body) noexcept {
        using Node = std::
            variant<const SemanticExpression*, const SemanticRegion*, const SemanticStatement*>;

        auto nodes = std::vector<Node> {body.residual ? &*body.residual : &body.region};
        const auto observe = [&](const SemanticExpression& expression) noexcept {
            if (!expression.operation_reachable) {
                return;
            }
            if (std::holds_alternative<SemAwait>(expression.value)
                || std::holds_alternative<SemWaitReady>(expression.value)
                || std::holds_alternative<SemCloseChild>(expression.value)) {
                mark(ordinal);
            }
            if (const auto* report = std::get_if<SemReport>(&expression.value); report != nullptr
                && (report->kind == ReportKind::Require || report->kind == ReportKind::Fail)) {
                mark(ordinal);
            }
            if (const auto* call = std::get_if<SemGenericCall>(&expression.value)) {
                const auto& target =
                    std::get<GenericCallableDefinition>(generics.definition(call->definition));
                if (target.signature.execution == CallableExecutionKind::Synchronous) {
                    callers[generic_ordinal(call->definition)].push_back(ordinal);
                }
            }
            if (const auto* call = std::get_if<SemCall>(&expression.value)) {
                if (const auto direct = direct_call_target(declarations, *call)) {
                    callers[direct->index()].push_back(ordinal);
                    return;
                }
                auto callee_type = call->callee->type.construction();
                if (const auto* generic = std::get_if<GenericTypeID>(&callee_type)) {
                    const auto& shape = generics.generic_type(*generic);
                    if (std::holds_alternative<GenericCallableViewType>(shape)) {
                        mark(ordinal);
                        return;
                    }
                    if (const auto* closure = std::get_if<GenericClosureType>(&shape)) {
                        callers[generic_ordinal(closure->definition)].push_back(ordinal);
                        return;
                    }
                    const auto* concrete = std::get_if<TypeID>(&shape);
                    if (!concrete) {
                        invariant_violation("checked indirect call lacks a callable type");
                    }
                    callee_type = *concrete;
                }
                const auto& type = types.type(resolved_types.resolve(callee_type)).value;
                if (const auto* function = std::get_if<FunctionTypeValue>(&type)) {
                    callers[function->callable.index()].push_back(ordinal);
                } else if (const auto* closure = std::get_if<ClosureTypeValue>(&type)) {
                    callers[closure->callable.index()].push_back(ordinal);
                } else if (std::holds_alternative<CallableViewTypeValue>(type)) {
                    mark(ordinal);
                }
            }
        };
        while (!nodes.empty()) {
            const auto next = nodes.back();
            nodes.pop_back();
            next.visit([&](const auto* node) noexcept {
                using Value = std::remove_cvref_t<decltype(*node)>;
                const auto add = [&](const auto& child) noexcept {
                    nodes.emplace_back(&child);
                };
                if constexpr (std::same_as<Value, SemanticRegion>) {
                    for (const auto& statement : node->statements) {
                        if (statement.reachable) {
                            add(statement);
                        }
                    }
                    if (node->result && node->result_reachable) {
                        add(*node->result);
                    }
                } else {
                    if constexpr (std::same_as<Value, SemanticStatement>) {
                        if (!node->reachable) {
                            return;
                        }
                    }
                    if constexpr (std::same_as<Value, SemanticExpression>) {
                        observe(*node);
                    }
                    visit_evaluation_children(node->value, add);
                }
            });
        }
    };
    for (const auto entry : declarations.callables()) {
        if (std::holds_alternative<CppImportImplementation>(entry.value.implementation)) {
            mark(entry.id.index());
        }
        if (const auto body = callable_body_id(entry.value)) {
            scan(entry.id.index(), *storage.bodies[body->index()].definition);
        }
    }
    for (const auto& [definition, body] : generics.bodies) {
        scan(generic_ordinal(definition), *body);
    }
    for (auto next = 0uz; next < pending.size(); ++next) {
        for (const auto caller : callers[pending[next]]) {
            mark(caller);
        }
    }
    stops.resize(callable_count);
    return stops;
}

auto ProgramDraft::verify_body(
    const SemIRBody& body,
    const DeclarationStore& declaration_view
) const noexcept -> void {
    const auto body_id = body.id();
    const auto identity = body.identity();
    const auto kind = body.kind();
    // Local IDs retain the checked source domain, including rigid definitions.
    const auto expected_identity = body.generic_definition()
        ? BodyIdentity(
              program_identity,
              body.generic_definition()->index(),
              BodyIdentityDomain::GenericDefinition
          )
        : BodyIdentity(
              program_identity,
              body.specialized().value_or(body_id).index(),
              BodyIdentityDomain::Body
          );
    if (body_id.owner() != program_identity
        || identity != expected_identity
        || (body.generic_definition() && body.generic_definition()->owner() != program_identity)) {
        invariant_violation("final body has inconsistent program/body identity evidence");
    }
    if (body.provenance_identity() != provenance_appender.reader().identity()) {
        invariant_violation("final body has foreign provenance identity evidence");
    }
    if (body_id.index() >= storage.bodies.size() || storage.bodies[body_id.index()].kind != kind) {
        invariant_violation("final body disagreed with its reservation metadata");
    }
    const auto callable = declaration_view.callable_for_body(body_id);
    if (kind == BodyKind::ConstBlock) {
        if (callable.has_value() || storage.bodies[body_id.index()].test.has_value()) {
            invariant_violation("const block has a callable or test owner");
        }
    } else if (kind == BodyKind::Test) {
        if (callable.has_value() || !storage.bodies[body_id.index()].test.has_value()) {
            invariant_violation("test body was not assigned exactly one test declaration");
        }
    } else {
        if (!callable.has_value()) {
            invariant_violation("function or closure body had no owning callable declaration");
        }
        const auto implementation = declaration_view.callable(*callable).implementation;
        const auto kind_matches =
            (kind == BodyKind::Function
             && std::holds_alternative<FunctionBodyImplementation>(implementation))
            || (kind == BodyKind::Closure
                && std::holds_alternative<ClosureBodyImplementation>(implementation));
        if (!kind_matches) {
            invariant_violation("body kind disagreed with its callable implementation");
        }
    }
}

auto ProgramDraft::resolve() && noexcept -> AnalysisResult<ResolvedAnalysisProgram> {
    require_state(State::Bodies, "solve construction");
    auto& input = storage;
    if (!input.pending_function_contracts.empty()
        || !input.declarations.construction_view().callable_contracts_complete()
        || !input.declarations.construction_view().callable_implementations_complete()
        || !std::ranges::all_of(
            input.bodies,
            [](const auto& slot) static noexcept { return slot.definition != nullptr; }
        )
        || !input.test_slots.all_defined()) {
        invariant_violation("construction solving began with incomplete reservations");
    }
    const auto valid_generics = validate_generic_definitions();
    if (!valid_generics) {
        return std::unexpected(valid_generics.error());
    }
    const auto closed = close_generic_calls();
    if (!closed) {
        return std::unexpected(closed.error());
    }
    const auto failure_members = validate_failure_members();
    if (!failure_members) {
        return std::unexpected(failure_members.error());
    }
    for (const auto& slot : input.generic_instances) {
        const auto* progress =
            std::get_if<ConstructionStorage::GenericInstanceSlot::Progress>(&slot.state);
        if (!progress
            || *progress != ConstructionStorage::GenericInstanceSlot::Progress::Complete) {
            invariant_violation("generic instance closure contains an incomplete reservation");
        }
        const auto definition = generic_declaration_copy(slot.instance.definition);
        definition.visit([&](const auto& source) noexcept {
            input.declarations.append_nominal_item(
                source.contract.module_id,
                slot.instance.declaration
            );
        });
    }
    const auto valid_sequence_elements = validate_vector_elements();
    if (!valid_sequence_elements) {
        return std::unexpected(valid_sequence_elements.error());
    }
    auto enum_roots = std::vector<TypeID>();
    for (const auto enumeration : enum_declaration_ids()) {
        enum_roots.push_back(intern_type({.value = EnumTypeValue {.enumeration = enumeration}}));
    }
    resolve_enum_equality(enum_roots);
    auto failures = solve_failure_constraints(
        std::move(input.failure_constraints).finish(),
        input.failure_sets,
        provenance_appender.reader(),
        FailureTypeDiagnosticNames(
            input.types,
            input.declarations.construction_view(),
            provenance_appender.reader()
        ),
        analysis_diagnostics,
        true
    );
    if (!failures.has_value()) {
        return std::unexpected(failures.error());
    }
    const auto failure_owner = std::make_shared<const FailureSolution>(std::move(*failures));
    const auto resolved_types = normalize_construction_types(*failure_owner);
    if (source_observer != nullptr) {
        source_observer->resolve_types(resolved_types);
    }
    auto generics = take_generic_analysis_snapshot(resolved_types, failure_owner);
    finalize_callable_signatures(resolved_types, *failure_owner);
    auto declarations = std::move(input.declarations).seal(resolved_types);
    auto types = std::move(input.types).seal();
    auto constants = std::move(input.constants).seal();
    auto failure_sets = std::move(input.failure_sets).seal();
    auto signatures = std::move(input.callable_signatures).seal();
    auto call_stops = solve_call_stops(types, resolved_types, declarations, generics);
    auto bodies = MutableProgramTable<SemIRBody, BodyID>(program_identity);
    for (auto& slot : input.bodies) {
        auto body = resolve_body(
            std::move(*slot.definition),
            resolved_types,
            types,
            declarations,
            call_stops,
            *failure_owner,
            failure_sets,
            provenance_appender.reader(),
            analysis_diagnostics
        );
        verify_body(body, declarations);
        const auto expected = body.id();
        if (bodies.add(std::move(body)) != expected) {
            invariant_violation("body publication changed its reserved identity");
        }
    }
    auto static_instances = std::vector<StaticInstance>();
    static_instances.reserve(input.static_instances.size());
    for (auto& slot : input.static_instances) {
        const auto* progress =
            std::get_if<ConstructionStorage::StaticInstanceSlot::Progress>(&slot.state);
        if (!progress || *progress != ConstructionStorage::StaticInstanceSlot::Progress::Complete) {
            invariant_violation("construction solving began with an incomplete static instance");
        }
        static_instances.push_back(std::move(slot.instance));
    }
    auto generic_declarations = std::vector<GenericDeclarationContract>();
    for (const auto [index, definition] : std::views::enumerate(input.generic_definitions)) {
        definition->visit([&](auto& source) noexcept {
            source.contract.implementation_dependencies = generic_implementation_dependencies(
                generics,
                GenericDeclarationID(program_identity, static_cast<std::uint32_t>(index)),
                failure_sets
            );
            generic_declarations.push_back(std::move(source.contract));
        });
    }
    auto generic_instances = std::vector<GenericNominalInstance>();
    for (auto& slot : input.generic_instances) {
        generic_instances.push_back(std::move(slot.instance));
    }
    auto generic_callables = std::vector<GenericCallableInstance>();
    for (auto& slot : input.generic_callable_instances) {
        const auto* progress =
            std::get_if<ConstructionStorage::GenericCallableInstanceSlot::Progress>(&slot.state);
        if (!progress
            || *progress != ConstructionStorage::GenericCallableInstanceSlot::Progress::Complete) {
            invariant_violation(
                "construction solving began with an incomplete generic callable instance"
            );
        }
        generic_callables.push_back(
            {.definition = slot.definition,
             .arguments = std::move(slot.arguments),
             .callable = slot.callable}
        );
    }
    auto final_bodies = BodyStore(std::move(bodies).seal());
    auto final_tests = TestStore(std::move(input.test_slots).seal());
    return ResolvedAnalysisProgram {
        .program = SemIRProgram(
            program_identity,
            std::move(provenance_appender).finish(),
            std::move(types),
            std::move(constants),
            std::move(failure_sets),
            std::move(signatures),
            std::move(declarations),
            std::move(final_bodies),
            std::move(final_tests),
            std::move(static_instances),
            std::move(generic_declarations),
            std::move(generic_instances),
            std::move(generic_callables),
            std::move(call_stops)
        ),
        .generics = std::move(generics)
    };
}

auto ProgramDraft::finalize_callable_signatures(
    const TypeResolution& types,
    const FailureSolution& failures
) noexcept -> void {
    const auto view = storage.declarations.construction_view();
    for (const auto callable_id : view.callable_ids()) {
        const auto& contract = view.callable_contract(callable_id);
        auto parameters = std::vector<CallableParameter>();
        parameters.reserve(contract.parameters.size());
        for (const auto& parameter : contract.parameters) {
            parameters.push_back(
                CallableParameter {
                    .stage = parameter.stage,
                    .access = parameter.access,
                    .type = types.resolve(parameter.type),
                }
            );
        }
        const auto signature = storage.callable_signatures.intern(
            CallableSignature {
                .execution = contract.execution,
                .parameters = std::move(parameters),
                .result = types.resolve(contract.result),
                .failures = failures.failure_set(contract.failures),
            }
        );
        storage.declarations.define_callable_signature(callable_id, signature);
    }
    storage.declarations.finish_callable_signatures();
}
