module carven:semantic.semir.program;

import :semantic.semir.async;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.stage;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.module_path;
import :source.provenance;
import :source.provenance.ids;
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

    auto entries() const noexcept {
        return rows.entries() | std::views::filter([](const auto entry) static noexcept {
                   return entry.value != nullptr;
               })
            | std::views::transform([](const auto entry) static noexcept {
                   return IDTableEntry<BodyID, SemIRBody> {.id = entry.id, .value = *entry.value};
               });
    }

    auto size() const noexcept -> std::size_t;

private:
    explicit BodyStore(ImmutableProgramTable<SemIRBody, BodyID> values) noexcept;

    // Dropping analysis-only bodies leaves vacant IDs, never changes a
    // surviving body's identity, and releases the entire removed allocation.
    ImmutableProgramTable<std::unique_ptr<SemIRBody>, BodyID> rows;

    friend class ProgramDraft;
    friend class SemIRProgram;
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
    friend class SemIRProgram;
};

// Where the executable definition of a callable lives.
enum class DefinitionPlacement {
    // A function with static parameters executes only through its instances.
    None,
    // One definition, in the artifact of the owning module.
    Owner,
    // One inline definition in every artifact that uses it.
    Use,
};

// The source-level references that an inline definition may expose, independent
// of the static arguments selected by any caller.
struct CallableSurface final {
    std::vector<TypeID> types;
    std::vector<CallableID> callables;
    // Source construction order, retained independently of specialization.
    std::vector<CallableID> closures;
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
    auto type_contents(TypeID type) const noexcept -> const TypeContents&;
    auto constants() const noexcept -> const ConstantStore&;
    auto failure_sets() const noexcept -> const FailureSetStore&;
    auto callable_signatures() const noexcept -> const CallableSignatureStore&;
    auto declarations() const noexcept -> const DeclarationStore&;
    auto bodies() const noexcept -> const BodyStore&;
    auto tests() const noexcept -> const TestStore&;
    // A staged function has static parameters and executes only through instances.
    auto is_staged(FunctionID function) const noexcept -> bool;
    auto static_instances() const noexcept -> std::span<const StaticInstance>;
    // The function a callable implements: its declaration, or the function
    // an instance was produced from.
    auto source_function(CallableID callable) const noexcept -> std::optional<FunctionID>;
    auto definition_placement(CallableID callable) const noexcept -> DefinitionPlacement;
    auto callable_surface(CallableID callable) const noexcept -> const CallableSurface&;
    // Before publication, distinguishes executable bodies from source templates
    // and static roots. Every surviving published body returns true; removed
    // bodies are absent from bodies() and cannot be queried here.
    auto executes(BodyID body) const noexcept -> bool;
    // The instance a callable is, when static specialization produced it.
    auto static_instance(CallableID callable) const noexcept -> const StaticInstance*;
    auto find_static_instance(
        FunctionID function,
        std::span<const ConstantID> arguments
    ) const noexcept -> std::optional<CallableID>;
    // Outward cancellation of execution, independent of nominal failures.
    auto may_complete_cancelled(CallableID callable) const noexcept -> bool;
    // Completion observed by this await, excluding evaluation of its operand.
    auto await_completion_may_be_cancelled(BodyID body, const SemAwait& occurrence) const noexcept
        -> bool;
    auto may_stop_test(CallableID callable_id) const noexcept -> bool;
    auto may_stop_test(TypeID type) const noexcept -> bool;
    auto may_stop_test(const SemCall& call) const noexcept -> bool;
    auto call_signature(TypeID type) const noexcept -> CallableSignatureID;
    auto call_signature(const SemCall& call) const noexcept -> CallableSignatureID;

private:
    auto publish_surfaces() noexcept -> void;
    auto publish_bodies() noexcept -> void;
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
        std::vector<StaticInstance> static_instances,
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
    std::vector<StaticInstance> static_instance_store;
    std::map<std::pair<FunctionID, std::vector<ConstantID>>, CallableID> static_instance_index;
    std::map<CallableID, std::size_t> instance_callables;
    std::vector<bool> executed_bodies;
    std::vector<bool> test_stops;
    std::unique_ptr<const AsyncCancellationFacts> cancellation_facts;
    std::vector<TypeContents> contents;
    std::vector<CallableSurface> callable_surfaces;

    friend class ProgramDraft;
};
