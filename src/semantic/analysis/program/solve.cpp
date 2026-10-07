module carven:semantic.analysis.program.solve.impl;

import :semantic.analysis.body.resolve;
import :semantic.analysis.program;
import :semantic.semir.children;
import :semantic.semir.evaluation;
import :support.invariant;
import std;

auto ProgramDraft::solve_test_stops(
    const CanonicalTypeStore& types,
    const TypeResolution& resolved_types,
    const DeclarationStore& declarations,
    const GenericAnalysisSnapshot& generics
) const noexcept -> TestStopFacts {
    const auto callable_count =
        static_cast<std::size_t>(std::ranges::distance(declarations.callables()));
    const auto count = callable_count + generics.definitions.size();
    auto stops = std::vector<bool>(count, false);
    auto callers = std::vector<std::vector<std::size_t>>(count);
    auto pending = std::vector<std::size_t>();
    const auto generic_ordinal = [&](GenericDeclarationID definition) noexcept {
        static_cast<void>(generics.definition(definition));
        return callable_count + definition.index();
    };
    const auto mark = [&](std::size_t ordinal) noexcept {
        if (!stops[ordinal]) {
            stops[ordinal] = true;
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
            if (const auto* report = std::get_if<SemReport>(&expression.value); report != nullptr
                && (report->kind == ReportKind::Require || report->kind == ReportKind::Fail)) {
                mark(ordinal);
            }
            if (const auto* call = std::get_if<SemGenericCall>(&expression.value)) {
                const auto& target =
                    std::get<GenericFunctionDefinition>(generics.definition(call->definition));
                if (target.signature.execution == CallableExecutionKind::Synchronous) {
                    callers[generic_ordinal(call->definition)].push_back(ordinal);
                }
            }
            if (const auto* call = std::get_if<SemCall>(&expression.value)) {
                if (call->target) {
                    callers[call->target->index()].push_back(ordinal);
                    return;
                }
                auto callee_type = call->callee->type.construction();
                if (const auto* generic = std::get_if<GenericTypeID>(&callee_type)) {
                    const auto* concrete = std::get_if<TypeID>(&generics.generic_type(*generic));
                    if (!concrete) {
                        invariant_violation("checked indirect call lacks a concrete callable type");
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
    auto result = TestStopFacts {
        .callables = std::vector<bool>(
            stops.begin(),
            stops.begin() + static_cast<std::ptrdiff_t>(callable_count)
        ),
        .definitions = {},
    };
    for (const auto& [definition, body] : generics.bodies) {
        static_cast<void>(body);
        result.definitions.emplace(definition, stops[generic_ordinal(definition)]);
    }
    return result;
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
        : BodyIdentity(program_identity, body.specialized().value_or(body_id).index());
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
    const auto valid_sequence_elements = validate_sequence_elements();
    if (!valid_sequence_elements) {
        return std::unexpected(valid_sequence_elements.error());
    }
    auto enum_roots = std::vector<TypeID>();
    for (const auto enumeration : enum_declaration_ids()) {
        enum_roots.push_back(intern_type({.value = EnumTypeValue {enumeration}}));
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
        analysis_diagnostics
    );
    if (!failures.has_value()) {
        return std::unexpected(failures.error());
    }
    auto failure_owner = std::make_shared<const FailureSolution>(std::move(*failures));
    const auto resolved_types = normalize_construction_types(*failure_owner);
    auto generics = take_generic_analysis_snapshot(resolved_types, failure_owner);
    finalize_callable_signatures(resolved_types, *failure_owner);
    auto declarations = std::move(input.declarations).seal(resolved_types);
    auto types = std::move(input.types).seal();
    auto constants = std::move(input.constants).seal();
    auto failure_sets = std::move(input.failure_sets).seal();
    auto signatures = std::move(input.callable_signatures).seal();
    auto stop_facts = solve_test_stops(types, resolved_types, declarations, generics);
    generics.test_stops = std::move(stop_facts.definitions);
    auto test_stops = std::move(stop_facts.callables);
    auto bodies = MutableProgramTable<SemIRBody, BodyID>(program_identity);
    for (auto& slot : input.bodies) {
        auto body = resolve_body(
            std::move(*slot.definition),
            resolved_types,
            types,
            test_stops,
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
    for (auto& definition : input.generic_definitions) {
        definition->visit([&](auto& source) noexcept {
            generic_declarations.push_back(std::move(source.contract));
        });
    }
    auto generic_instances = std::vector<GenericNominalInstance>();
    for (auto& slot : input.generic_instances) {
        generic_instances.push_back(std::move(slot.instance));
    }
    auto final_bodies = BodyStore(std::move(bodies).seal());
    auto final_tests = TestStore(std::move(input.test_slots).seal());
    return ResolvedAnalysisProgram {
        SemIRProgram(
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
            std::move(test_stops)
        ),
        std::move(generics)
    };
}

auto ProgramDraft::finalize_callable_signatures(
    const TypeResolution& types,
    const FailureSolution& failures
) noexcept -> void {
    const auto view = storage.declarations.construction_view();
    for (const auto callable_id : view.callable_ids()) {
        const auto contract = view.callable_contract(callable_id);
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
