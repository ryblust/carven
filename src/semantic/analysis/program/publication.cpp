module carven:semantic.analysis.program.publication.impl;

import :semantic.analysis.program;
import :semantic.semir.publication;
import :support.invariant;
import std;

auto ProgramDraft::seal() && noexcept -> SemIRProgram {
    require_state(State::Solved, "seal semantic program");
    auto& data = std::get<FinalStorage>(storage);
    validate_semantic_storage(
        program_identity,
        provenance_appender.reader(),
        data.types,
        data.constants,
        data.failure_sets,
        data.callable_signatures,
        data.declarations,
        data.bodies,
        data.tests
    );
    state = State::Sealed;
    return SemIRProgram(
        program_identity,
        std::move(provenance_appender).finish(),
        std::move(data.types),
        std::move(data.constants),
        std::move(data.failure_sets),
        std::move(data.callable_signatures),
        std::move(data.declarations),
        std::move(data.bodies),
        std::move(data.tests)
    );
}
