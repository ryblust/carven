module carven:semantic.semir.program;

import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.module_path;
import :source.provenance.ids;
import :source.provenance;
import :source.text;
import std;

class BodyStore final {
public:
    BodyStore(const BodyStore&) = delete;
    BodyStore(BodyStore&&) = default;
    ~BodyStore() = default;
    auto operator=(const BodyStore&) -> BodyStore& = delete;
    auto operator=(BodyStore&&) -> BodyStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(BodyID id) const noexcept -> bool;
    auto body(BodyID id) const noexcept -> const SemIRBody&;
    auto entries() const noexcept -> IDTableEntries<BodyID, SemIRBody, ProgramIdentity>;
    auto size() const noexcept -> std::size_t;

private:
    explicit BodyStore(ImmutableProgramTable<SemIRBody, BodyID> values) noexcept;

    ImmutableProgramTable<SemIRBody, BodyID> rows;

    friend class ProgramDraft;
};

class TestStore final {
public:
    TestStore(const TestStore&) = delete;
    TestStore(TestStore&&) = default;
    ~TestStore() = default;
    auto operator=(const TestStore&) -> TestStore& = delete;
    auto operator=(TestStore&&) -> TestStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(TestID id) const noexcept -> bool;
    auto test(TestID id) const noexcept -> const TestDeclaration&;
    auto entries() const noexcept -> IDTableEntries<TestID, TestDeclaration, ProgramIdentity>;
    auto size() const noexcept -> std::size_t;

private:
    explicit TestStore(ImmutableProgramTable<TestDeclaration, TestID> values) noexcept;

    ImmutableProgramTable<TestDeclaration, TestID> rows;

    friend class ProgramDraft;
};

class SemIRProgram final {
public:
    SemIRProgram(const SemIRProgram&) = delete;
    SemIRProgram(SemIRProgram&&) noexcept = default;
    ~SemIRProgram() = default;
    auto operator=(const SemIRProgram&) -> SemIRProgram& = delete;
    auto operator=(SemIRProgram&&) -> SemIRProgram& = delete;
    auto identity() const noexcept -> ProgramIdentity;
    auto provenance() const noexcept -> CompilationProvenanceView;
    auto types() const noexcept -> const CanonicalTypeStore&;
    auto constants() const noexcept -> const ConstantStore&;
    auto failure_sets() const noexcept -> const FailureSetStore&;
    auto callable_signatures() const noexcept -> const CallableSignatureStore&;
    auto declarations() const noexcept -> const DeclarationStore&;
    auto bodies() const noexcept -> const BodyStore&;
    auto tests() const noexcept -> const TestStore&;
    auto may_stop_test(CallableID callable_id) const noexcept -> bool;
    auto may_stop_test(TypeID type) const noexcept -> bool;
    auto call_signature(TypeID type) const noexcept -> CallableSignatureID;
    auto may_stop_test(const SemanticExpression& expression) const noexcept -> bool;

private:
    SemIRProgram(
        ProgramIdentity identity,
        CompilationProvenance provenance,
        CanonicalTypeStore types,
        ConstantStore constants,
        FailureSetStore failure_sets,
        CallableSignatureStore callable_signatures,
        DeclarationStore declarations,
        BodyStore bodies,
        TestStore tests,
        std::vector<bool> test_stops
    ) noexcept;

    ProgramIdentity program_identity;
    CompilationProvenance compilation_provenance;
    CanonicalTypeStore type_store;
    ConstantStore constant_store;
    FailureSetStore failure_set_store;
    CallableSignatureStore callable_signature_store;
    DeclarationStore declaration_store;
    BodyStore body_store;
    TestStore test_store;
    std::vector<bool> test_stops;

    friend class ProgramDraft;
};
