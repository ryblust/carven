module carven:semantic.semir.decl;

import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.table;
import :semantic.semir.type;
import :semantic.visibility;
import :source.provenance.ids;
import std;

enum class CppHeaderDelimiter {
    AngleBrackets,
    Quotes,
};

struct CppNamespaceOpening final {
    std::vector<ProgramSpellingID> components;
    ProgramOriginID origin;
};

struct CppHeaderDependency final {
    CppHeaderDelimiter delimiter;
    ProgramSpellingID name;
    ProgramOriginID origin;
    std::optional<CppNamespaceOpening> namespace_opening;
};

struct CppSourceFragment final {
    ProgramOriginID payload_origin;
};

using DeclarationRef = std::variant<FunctionID, StructID, EnumID, ModuleConstantID>;
using NominalDeclarationRef = std::variant<StructID, EnumID>;
using ModuleItem = std::variant<FunctionID, StructID, EnumID, ModuleConstantID, TestID>;

struct ModuleDeclaration final {
    ProgramModuleID provenance_module;
    ProgramOriginID origin;
    std::vector<CppHeaderDependency> cpp_headers;
    std::vector<CppSourceFragment> cpp_source_fragments;
    std::vector<ModuleItem> items;
};

enum class EntryPointKind {
    NoArguments,
    WithArguments,
};

struct FunctionDeclaration final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    CallableID callable;
    std::optional<EntryPointKind> entry_point;
    std::optional<ProgramOriginID> cpp_export_origin;
};

struct StructField final {
    ProgramSpellingID name;
    TypeID type;
    ProgramOriginID origin;
};

struct ConstructionStructField final {
    ProgramSpellingID name;
    ConstructionTypeRef type;
    ProgramOriginID origin;
};

struct NominalCapabilities final {
    bool equality;
};

struct StructDeclaration final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    std::vector<StructField> fields;
    NominalCapabilities capabilities;
};

struct ConstructionStructDeclaration final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    std::vector<ConstructionStructField> fields;
    NominalCapabilities capabilities;
};

struct NumericEnumRepresentation final {
    TypeID underlying_type;
};

struct PayloadEnumRepresentation final {};

using EnumRepresentation = std::variant<NumericEnumRepresentation, PayloadEnumRepresentation>;

struct EnumDeclaration final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    EnumRepresentation representation;
    std::vector<EnumCaseID> cases;
    NominalCapabilities capabilities;
};

struct EnumCaseDeclaration final {
    EnumID owner;
    ProgramSpellingID name;
    ProgramOriginID origin;
    std::vector<TypeID> payload_types;
    std::optional<ConstantID> constant;
};

struct ConstructionEnumCaseDeclaration final {
    EnumID owner;
    ProgramSpellingID name;
    ProgramOriginID origin;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};

struct ModuleConstantDeclaration final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ConstantID value;
};

struct FunctionBodyImplementation final {
    BodyID body;
};

struct ClosureBodyImplementation final {
    BodyID body;
};

struct CppImportImplementation final {
    ProgramOriginID form_origin;
};

using CallableImplementation =
    std::variant<FunctionBodyImplementation, ClosureBodyImplementation, CppImportImplementation>;

struct CallableDeclaration final {
    CallableSignatureID signature;
    CallableImplementation implementation;
};

auto callable_body_id(const CallableDeclaration& callable) noexcept -> std::optional<BodyID>;
auto cpp_import_form_origin(const CallableDeclaration& callable) noexcept
    -> std::optional<ProgramOriginID>;

struct TestDeclaration final {
    ModuleID module_id;
    ProgramSpellingID name;
    ProgramOriginID origin;
    BodyID body;
};

enum class FailureContractPolicy {
    Declared,
    Inferred,
    UndeclaredExplicit,
};

struct ConstructionCallableContract final {
    std::vector<ConstructionCallableParameter> parameters;
    ConstructionTypeRef result;
    FailureTermID failures;
    FailureContractPolicy policy;
};

class DeclarationStore final {
public:
    DeclarationStore(const DeclarationStore&) = delete;
    DeclarationStore(DeclarationStore&&) = default;
    ~DeclarationStore() = default;
    auto operator=(const DeclarationStore&) -> DeclarationStore& = delete;
    auto operator=(DeclarationStore&&) -> DeclarationStore& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto contains(ModuleID id) const noexcept -> bool;
    auto contains(FunctionID id) const noexcept -> bool;
    auto contains(StructID id) const noexcept -> bool;
    auto contains(EnumID id) const noexcept -> bool;
    auto contains(EnumCaseID id) const noexcept -> bool;
    auto contains(ModuleConstantID id) const noexcept -> bool;
    auto contains(CallableID id) const noexcept -> bool;
    auto module_decl(ModuleID id) const noexcept -> const ModuleDeclaration&;
    auto function(FunctionID id) const noexcept -> const FunctionDeclaration&;
    auto structure(StructID id) const noexcept -> const StructDeclaration&;
    auto enumeration(EnumID id) const noexcept -> const EnumDeclaration&;
    auto enum_case(EnumCaseID id) const noexcept -> const EnumCaseDeclaration&;
    auto module_constant(ModuleConstantID id) const noexcept -> const ModuleConstantDeclaration&;
    auto callable(CallableID id) const noexcept -> const CallableDeclaration&;
    auto body_for_callable(CallableID callable) const noexcept -> std::optional<BodyID>;
    auto callable_for_body(BodyID body) const noexcept -> std::optional<CallableID>;
    auto modules() const noexcept -> IDTableEntries<ModuleID, ModuleDeclaration, ProgramIdentity>;
    auto functions() const noexcept
        -> IDTableEntries<FunctionID, FunctionDeclaration, ProgramIdentity>;
    auto structures() const noexcept
        -> IDTableEntries<StructID, StructDeclaration, ProgramIdentity>;
    auto enumerations() const noexcept -> IDTableEntries<EnumID, EnumDeclaration, ProgramIdentity>;
    auto enum_cases() const noexcept
        -> IDTableEntries<EnumCaseID, EnumCaseDeclaration, ProgramIdentity>;
    auto module_constants() const noexcept
        -> IDTableEntries<ModuleConstantID, ModuleConstantDeclaration, ProgramIdentity>;
    auto callables() const noexcept
        -> IDTableEntries<CallableID, CallableDeclaration, ProgramIdentity>;

private:
    DeclarationStore(
        ImmutableProgramTable<ModuleDeclaration, ModuleID> modules,
        ImmutableProgramTable<FunctionDeclaration, FunctionID> functions,
        ImmutableProgramTable<StructDeclaration, StructID> structures,
        ImmutableProgramTable<EnumDeclaration, EnumID> enumerations,
        ImmutableProgramTable<EnumCaseDeclaration, EnumCaseID> enum_cases,
        ImmutableProgramTable<ModuleConstantDeclaration, ModuleConstantID> module_constants,
        ImmutableProgramTable<CallableDeclaration, CallableID> callables,
        std::map<BodyID, CallableID> body_callables
    ) noexcept;

    ImmutableProgramTable<ModuleDeclaration, ModuleID> module_rows;
    ImmutableProgramTable<FunctionDeclaration, FunctionID> function_rows;
    ImmutableProgramTable<StructDeclaration, StructID> struct_rows;
    ImmutableProgramTable<EnumDeclaration, EnumID> enum_rows;
    ImmutableProgramTable<EnumCaseDeclaration, EnumCaseID> enum_case_rows;
    ImmutableProgramTable<ModuleConstantDeclaration, ModuleConstantID> module_constant_rows;
    ImmutableProgramTable<CallableDeclaration, CallableID> callable_rows;
    std::map<BodyID, CallableID> body_callables;

    friend class DeclarationBuilder;
};

class DeclarationBuilder;

class DeclarationConstructionView final {
public:
    auto owner() const noexcept -> ProgramIdentity;
    auto module_decl(ModuleID id) const noexcept -> ModuleDeclaration;
    auto function(FunctionID id) const noexcept -> FunctionDeclaration;
    auto structure(StructID id) const noexcept -> ConstructionStructDeclaration;
    auto enumeration(EnumID id) const noexcept -> EnumDeclaration;
    auto enum_case(EnumCaseID id) const noexcept -> ConstructionEnumCaseDeclaration;
    auto module_constant(ModuleConstantID id) const noexcept -> ModuleConstantDeclaration;
    auto callable_contract(CallableID id) const noexcept -> ConstructionCallableContract;
    auto callable_signature(CallableID id) const noexcept -> CallableSignatureID;
    auto callable_implementation(CallableID id) const noexcept -> CallableImplementation;
    auto module_count() const noexcept -> std::size_t;
    auto function_count() const noexcept -> std::size_t;
    auto struct_count() const noexcept -> std::size_t;
    auto enum_count() const noexcept -> std::size_t;
    auto enum_case_count() const noexcept -> std::size_t;
    auto module_constant_count() const noexcept -> std::size_t;
    auto callable_count() const noexcept -> std::size_t;
    auto module_ids() const noexcept -> std::vector<ModuleID>;
    auto function_ids() const noexcept -> std::vector<FunctionID>;
    auto struct_ids() const noexcept -> std::vector<StructID>;
    auto enum_ids() const noexcept -> std::vector<EnumID>;
    auto enum_case_ids() const noexcept -> std::vector<EnumCaseID>;
    auto module_constant_ids() const noexcept -> std::vector<ModuleConstantID>;
    auto callable_ids() const noexcept -> std::vector<CallableID>;
    auto callable_contract_defined(CallableID id) const noexcept -> bool;
    auto callable_contracts_complete() const noexcept -> bool;
    auto callable_implementations_complete() const noexcept -> bool;

private:
    explicit DeclarationConstructionView(const DeclarationBuilder& builder) noexcept;

    const DeclarationBuilder* declaration_builder;

    friend class DeclarationBuilder;
};

class DeclarationBuilder final {
public:
    DeclarationBuilder(ProgramIdentity owner, ProvenanceIdentity provenance) noexcept;
    DeclarationBuilder(const DeclarationBuilder&) = delete;
    DeclarationBuilder(DeclarationBuilder&&) = default;
    ~DeclarationBuilder() = default;
    auto operator=(const DeclarationBuilder&) -> DeclarationBuilder& = delete;
    auto operator=(DeclarationBuilder&&) -> DeclarationBuilder& = delete;
    auto owner() const noexcept -> ProgramIdentity;
    auto reserve_module() noexcept -> ModuleID;
    auto reserve_function() noexcept -> FunctionID;
    auto reserve_struct() noexcept -> StructID;
    auto reserve_enum() noexcept -> EnumID;
    auto reserve_enum_case() noexcept -> EnumCaseID;
    auto reserve_module_constant() noexcept -> ModuleConstantID;
    auto reserve_callable() noexcept -> CallableID;
    auto define(ModuleID id, ModuleDeclaration declaration) noexcept -> void;
    auto define(FunctionID id, FunctionDeclaration declaration) noexcept -> void;
    auto define(StructID id, ConstructionStructDeclaration declaration) noexcept -> void;
    auto define(EnumID id, EnumDeclaration declaration) noexcept -> void;
    auto define(EnumCaseID id, ConstructionEnumCaseDeclaration declaration) noexcept -> void;
    auto define(ModuleConstantID id, ModuleConstantDeclaration declaration) noexcept -> void;
    auto define_callable_contract(CallableID id, ConstructionCallableContract contract) noexcept
        -> void;
    auto finish_heads() noexcept -> DeclarationConstructionView;
    auto construction_view() const noexcept -> DeclarationConstructionView;
    auto append_body_callable(ConstructionCallableContract contract) noexcept -> CallableID;
    auto define_callable_signature(CallableID id, CallableSignatureID signature) noexcept -> void;
    auto finish_callable_signatures() noexcept -> void;
    auto complete_callable(CallableID id, CallableImplementation implementation) noexcept -> void;
    auto seal(const TypeResolution& type_resolution) && noexcept -> DeclarationStore;

private:
    enum class State {
        Reserving,
        BuildingCallables,
        Concrete,
    };

    auto require_reserving() const noexcept -> void;
    auto require_building_callables() const noexcept -> void;
    auto require_heads_finished() const noexcept -> void;
    auto require_concrete() const noexcept -> void;
    auto require_heads_defined() const noexcept -> void;
    auto reserve_callable_pair() noexcept -> CallableID;

    ProgramIdentity program_identity;
    ProvenanceIdentity provenance_identity;
    State state;
    ReservedProgramTable<ModuleDeclaration, ModuleID> modules;
    ReservedProgramTable<FunctionDeclaration, FunctionID> functions;
    ReservedProgramTable<ConstructionStructDeclaration, StructID> structures;
    ReservedProgramTable<EnumDeclaration, EnumID> enumerations;
    ReservedProgramTable<ConstructionEnumCaseDeclaration, EnumCaseID> enum_cases;
    ReservedProgramTable<ModuleConstantDeclaration, ModuleConstantID> module_constants;
    ReservedProgramTable<ConstructionCallableContract, CallableID> callable_contracts;
    ReservedProgramTable<CallableSignatureID, CallableID> callable_signature_ids;
    ReservedProgramTable<CallableImplementation, CallableID> callable_implementations;
    // Derived while completing implementations, then moved into the final store.
    std::map<BodyID, CallableID> body_callables;
    std::vector<CallableID> callable_order;

    friend class DeclarationConstructionView;
};
