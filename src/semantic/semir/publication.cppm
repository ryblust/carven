module carven:semantic.semir.publication;

import :semantic.semir.program;

auto validate_semantic_storage(
    ProgramIdentity identity,
    CompilationProvenanceReader provenance,
    const CanonicalTypeStore& types,
    const ConstantStore& constants,
    const FailureSetStore& failures,
    const CallableSignatureStore& signatures,
    const DeclarationStore& declarations,
    const BodyStore& bodies,
    const TestStore& tests
) noexcept -> void;
