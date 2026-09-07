module carven:semantic.semir.program.impl;

import :semantic.semir.constant;
import :semantic.semir.program;
import :support.invariant;
import std;

SemIRProgram::SemIRProgram(
    ProgramIdentity identity,
    CompilationProvenance provenance,
    CanonicalTypeStore types,
    ConstantStore constants,
    FailureSetStore failure_sets,
    CallableSignatureStore callable_signatures,
    DeclarationStore declarations,
    BodyStore bodies,
    TestStore tests
) noexcept
    : program_identity(identity),
      compilation_provenance(std::move(provenance)),
      type_store(std::move(types)),
      constant_store(std::move(constants)),
      failure_set_store(std::move(failure_sets)),
      callable_signature_store(std::move(callable_signatures)),
      declaration_store(std::move(declarations)),
      body_store(std::move(bodies)),
      test_store(std::move(tests)),
      active(true) {}

SemIRProgram::SemIRProgram(SemIRProgram&& other) noexcept
    : program_identity(other.program_identity),
      compilation_provenance(std::move(other.compilation_provenance)),
      type_store(std::move(other.type_store)),
      constant_store(std::move(other.constant_store)),
      failure_set_store(std::move(other.failure_set_store)),
      callable_signature_store(std::move(other.callable_signature_store)),
      declaration_store(std::move(other.declaration_store)),
      body_store(std::move(other.body_store)),
      test_store(std::move(other.test_store)),
      active(std::exchange(other.active, false)) {
    if (!active) {
        invariant_violation("inactive semantic program was moved");
    }
}

auto SemIRProgram::operator=(SemIRProgram&& other) noexcept -> SemIRProgram& {
    if (this == std::addressof(other)) {
        invariant_violation("semantic program was moved into itself");
    }
    other.require_active();
    program_identity = other.program_identity;
    compilation_provenance = std::move(other.compilation_provenance);
    type_store = std::move(other.type_store);
    constant_store = std::move(other.constant_store);
    failure_set_store = std::move(other.failure_set_store);
    callable_signature_store = std::move(other.callable_signature_store);
    declaration_store = std::move(other.declaration_store);
    body_store = std::move(other.body_store);
    test_store = std::move(other.test_store);
    active = true;
    other.active = false;
    return *this;
}

auto SemIRProgram::require_active() const noexcept -> void {
    if (!active) {
        invariant_violation("semantic program was used after consumption");
    }
}

auto SemIRProgram::body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID> {
    require_active();
    const auto body = declaration_store.body_for_callable(callable);
    if (body.has_value() && !body_store.contains(*body)) {
        invariant_violation("callable referred to an unpublished body");
    }
    return body;
}

auto SemIRProgram::callable_for_body(BodyID body) const noexcept -> std::optional<CallableID> {
    require_active();
    if (!body_store.contains(body)) {
        invariant_violation("callable lookup used a foreign or unpublished body");
    }
    return declaration_store.callable_for_body(body);
}
