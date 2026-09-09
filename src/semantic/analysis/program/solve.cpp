module carven:semantic.analysis.program.solve.impl;

import :semantic.analysis.body.resolve;
import :semantic.analysis.program;
import :support.invariant;
import std;

auto ProgramDraft::verify_body(
    const SemIRBody& body,
    const DeclarationStore& declaration_view
) const noexcept -> void {
    const auto body_id = body.id();
    const auto identity = body.identity();
    const auto kind = body.kind();
    if (body_id.owner() != program_identity
        || identity.program() != program_identity
        || body_id.index() != identity.body_index()) {
        invariant_violation("final body has inconsistent program/body identity evidence");
    }
    if (body.provenance_identity() != provenance_appender.reader().identity()) {
        invariant_violation("final body has foreign provenance identity evidence");
    }
    if (body_id.index() >= storage.bodies.size() || storage.bodies[body_id.index()].kind != kind) {
        invariant_violation("final body disagreed with its reservation metadata");
    }
    const auto callable = declaration_view.callable_for_body(body_id);
    if (kind == BodyKind::Test) {
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

auto ProgramDraft::resolve() && noexcept -> AnalysisResult<SemIRProgram> {
    require_state(State::Bodies, "solve construction");
    auto& input = storage;
    if (!input.pending_function_contracts.empty()
        || !input.declarations.construction_view().callable_contracts_complete()
        || !input.declarations.construction_view().callable_implementations_complete()
        || !std::ranges::all_of(
            input.bodies,
            [](const auto& slot) static noexcept { return slot.definition.has_value(); }
        )
        || !input.test_slots.all_defined()) {
        invariant_violation("construction solving began with incomplete reservations");
    }
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
    const auto resolved_types =
        std::move(input.construction_types)
            .canonicalize(*failures, input.types, input.callable_signatures);
    finalize_callable_signatures(resolved_types, *failures);
    auto declarations = std::move(input.declarations).seal(resolved_types);
    auto types = std::move(input.types).seal();
    auto constants = std::move(input.constants).seal();
    auto failure_sets = std::move(input.failure_sets).seal();
    auto signatures = std::move(input.callable_signatures).seal();
    auto bodies = MutableProgramTable<SemIRBody, BodyID>(program_identity);
    for (auto& slot : input.bodies) {
        auto body = resolve_body(
            std::move(*slot.definition),
            resolved_types,
            *failures,
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
    auto final_bodies = BodyStore(std::move(bodies).seal());
    auto final_tests = TestStore(std::move(input.test_slots).seal());
    return SemIRProgram(
        program_identity,
        std::move(provenance_appender).finish(),
        std::move(types),
        std::move(constants),
        std::move(failure_sets),
        std::move(signatures),
        std::move(declarations),
        std::move(final_bodies),
        std::move(final_tests)
    );
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
                    .access = parameter.access,
                    .type = types.resolve(parameter.type),
                }
            );
        }
        const auto signature = storage.callable_signatures.intern(
            CallableSignature {
                .parameters = std::move(parameters),
                .result = types.resolve(contract.result),
                .failures = failures.failure_set(contract.failures),
            }
        );
        storage.declarations.define_callable_signature(callable_id, signature);
    }
    storage.declarations.finish_callable_signatures();
}
