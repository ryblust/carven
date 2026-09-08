module carven:semantic.analysis.program.solve.impl;

import :semantic.analysis.body.resolve;
import :semantic.analysis.program;
import :support.invariant;
import std;

auto ProgramDraft::complete_body(SemIRBody body, const DeclarationStore& declaration_view) noexcept
    -> void {
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
    if (!construction().body_slots.contains(body_id)
        || static_cast<std::size_t>(body_id.index()) >= construction().reserved_body_kinds.size()
        || construction().reserved_body_kinds[body_id.index()] != kind) {
        invariant_violation("final body disagreed with its reservation metadata");
    }
    const auto callable = declaration_view.callable_for_body(body_id);
    if (kind == BodyKind::Test) {
        if (callable.has_value() || !construction().test_by_body[body_id.index()].has_value()) {
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
    construction().body_slots.define(body_id, std::move(body));
}

auto ProgramDraft::solve_construction() noexcept -> AnalysisResult<void> {
    require_state(State::Bodies, "solve construction");
    auto& input = construction();
    if (!input.pending_function_contracts.empty()
        || !input.declarations.construction_view().callable_contracts_complete()
        || !input.declarations.construction_view().callable_implementations_complete()
        || input.body_drafts.size() != input.body_slots.size()
        || !input.test_slots.all_defined()) {
        invariant_violation("construction solving began with incomplete reservations");
    }
    state = State::Failed;
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
    for (auto& body : input.body_drafts) {
        complete_body(
            resolve_body(
                std::move(body),
                resolved_types,
                *failures,
                failure_sets,
                provenance_appender.reader(),
                analysis_diagnostics
            ),
            declarations
        );
    }
    auto final_bodies = BodyStore(std::move(input.body_slots).seal());
    auto final_tests = TestStore(std::move(input.test_slots).seal());
    storage.emplace<FinalStorage>(
        std::move(types),
        std::move(constants),
        std::move(failure_sets),
        std::move(signatures),
        std::move(declarations),
        std::move(final_bodies),
        std::move(final_tests)
    );
    state = State::Solved;
    return {};
}

auto ProgramDraft::finalize_callable_signatures(
    const TypeResolution& types,
    const FailureSolution& failures
) noexcept -> void {
    const auto view = construction().declarations.construction_view();
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
        const auto signature = construction().callable_signatures.intern(
            CallableSignature {
                .parameters = std::move(parameters),
                .result = types.resolve(contract.result),
                .failures = failures.failure_set(contract.failures),
            }
        );
        construction().declarations.define_callable_signature(callable_id, signature);
    }
    construction().declarations.finish_callable_signatures();
}
