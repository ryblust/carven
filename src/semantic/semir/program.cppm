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

    auto owner() const noexcept -> ProgramIdentity { return rows.owner(); }

    auto contains(BodyID id) const noexcept -> bool { return rows.contains(id); }

    auto body(BodyID id) const noexcept -> const SemIRBody& { return rows.get(id); }

    auto entries() const noexcept -> IDTableEntries<BodyID, SemIRBody, ProgramIdentity> {
        return rows.entries();
    }

    auto size() const noexcept -> std::size_t { return rows.size(); }

private:
    explicit BodyStore(ImmutableProgramTable<SemIRBody, BodyID> values) noexcept
        : rows(std::move(values)) {}

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

    auto owner() const noexcept -> ProgramIdentity { return rows.owner(); }

    auto contains(TestID id) const noexcept -> bool { return rows.contains(id); }

    auto test(TestID id) const noexcept -> const TestDeclaration& { return rows.get(id); }

    auto entries() const noexcept -> IDTableEntries<TestID, TestDeclaration, ProgramIdentity> {
        return rows.entries();
    }

    auto size() const noexcept -> std::size_t { return rows.size(); }

private:
    explicit TestStore(ImmutableProgramTable<TestDeclaration, TestID> values) noexcept
        : rows(std::move(values)) {}

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

    auto identity() const noexcept -> ProgramIdentity { return program_identity; }

    auto provenance() const noexcept -> CompilationProvenanceView {
        return compilation_provenance.view();
    }

    auto types() const noexcept -> const CanonicalTypeStore& { return type_store; }

    auto constants() const noexcept -> const ConstantStore& { return constant_store; }

    auto failure_sets() const noexcept -> const FailureSetStore& { return failure_set_store; }

    auto callable_signatures() const noexcept -> const CallableSignatureStore& {
        return callable_signature_store;
    }

    auto declarations() const noexcept -> const DeclarationStore& { return declaration_store; }

    auto bodies() const noexcept -> const BodyStore& { return body_store; }

    auto tests() const noexcept -> const TestStore& { return test_store; }


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
        TestStore tests
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

    friend class ProgramDraft;
};
