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
    auto operator=(BodyStore&&) -> BodyStore& = default;

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
    auto operator=(TestStore&&) -> TestStore& = default;

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
    SemIRProgram(SemIRProgram&& other) noexcept;
    ~SemIRProgram() = default;

    auto operator=(const SemIRProgram&) -> SemIRProgram& = delete;
    auto operator=(SemIRProgram&& other) noexcept -> SemIRProgram&;

    auto identity() const noexcept -> ProgramIdentity {
        require_active();
        return program_identity;
    }

    auto provenance() const noexcept -> CompilationProvenanceView {
        require_active();
        return compilation_provenance.view();
    }

    auto types() const noexcept -> const CanonicalTypeStore& {
        require_active();
        return type_store;
    }

    auto constants() const noexcept -> const ConstantStore& {
        require_active();
        return constant_store;
    }

    auto failure_sets() const noexcept -> const FailureSetStore& {
        require_active();
        return failure_set_store;
    }

    auto callable_signatures() const noexcept -> const CallableSignatureStore& {
        require_active();
        return callable_signature_store;
    }

    auto declarations() const noexcept -> const DeclarationStore& {
        require_active();
        return declaration_store;
    }

    auto bodies() const noexcept -> const BodyStore& {
        require_active();
        return body_store;
    }

    auto tests() const noexcept -> const TestStore& {
        require_active();
        return test_store;
    }

    auto body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID>;
    auto callable_for_body(BodyID body) const noexcept -> std::optional<CallableID>;

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
    auto require_active() const noexcept -> void;

    ProgramIdentity program_identity;
    CompilationProvenance compilation_provenance;
    CanonicalTypeStore type_store;
    ConstantStore constant_store;
    FailureSetStore failure_set_store;
    CallableSignatureStore callable_signature_store;
    DeclarationStore declaration_store;
    BodyStore body_store;
    TestStore test_store;
    bool active;

    friend class ProgramDraft;
};
