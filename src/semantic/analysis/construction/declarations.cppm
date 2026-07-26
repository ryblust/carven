module carven:semantic.analysis.declaration_construction;

import :semantic.analysis.builder;
import :semantic.analysis.catalog;
import :semantic.hir.decl;
import :semantic.hir.ids;
import :source.provenance.ids;
import :source.text;
import std;

class ModuleAnalysis;
class DeclarationSessionState;

struct FunctionContractState final {
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ProgramSpellingID name;
    CallableID callable;
    HIRTypeID result;
    ProgramOriginID result_origin;
    SymbolID symbol;
    std::optional<HIREntryPointKind> entry_point;
};

struct StructContractState final {
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ProgramSpellingID name;
    std::vector<HIRStructField> fields;
    SymbolID symbol;
};

struct EnumContractState final {
    ProgramOriginID origin;
    DeclarationVisibility visibility;
    ProgramSpellingID name;
    std::optional<HIRTypeID> underlying_type;
    std::vector<EnumCaseID> cases;
    HIREnumProfile profile;
    SymbolID symbol;
};

struct EnumCaseContractState final {
    EnumID owner;
    ProgramSpellingID name;
    std::vector<HIRTypeID> payload_types;
    SymbolID symbol;
    ProgramOriginID origin;
};

struct FunctionContractView final {
    CallableID callable;
    SymbolID symbol;
    std::optional<HIREntryPointKind> entry_point;
};

struct StructContractView final {
    std::span<const HIRStructField> fields;
    SymbolID symbol;
};

struct EnumContractView final {
    HIREnumProfile profile;
    std::optional<HIRTypeID> underlying_type;
    std::span<const EnumCaseID> cases;
    SymbolID symbol;
};

struct EnumCaseContractView final {
    EnumID owner;
    ProgramSpellingID name;
    std::span<const HIRTypeID> payload_types;
    SymbolID symbol;
    ProgramOriginID origin;
};

class DeclarationSessionView final {
public:
    auto function(FunctionID id) const noexcept -> FunctionContractView;
    auto structure(StructID id) const noexcept -> StructContractView;
    auto enumeration(EnumID id) const noexcept -> EnumContractView;
    auto enum_case(EnumCaseID id) const noexcept -> EnumCaseContractView;
    auto enum_count() const noexcept -> std::size_t;
    auto enum_case_count() const noexcept -> std::size_t;
    auto supports_equality(HIRTypeID id) const noexcept -> bool;
    auto resolve(
        SymbolID symbol_id,
        ProgramModuleID requester_module_id,
        Span origin
    ) const noexcept -> bool;

private:
    explicit DeclarationSessionView(DeclarationSessionState& state) noexcept;

    DeclarationSessionState* session_state;

    friend class DeclarationConstruction;
    friend class ResolvedDeclarations;
};

class DeclarationDefinitionSink final {
public:
    auto define(FunctionID id, FunctionContractState contract) noexcept -> void;
    auto define(StructID id, StructContractState contract) noexcept -> void;
    auto define(EnumID id, EnumContractState contract) noexcept -> void;
    auto define(EnumCaseID id, EnumCaseContractState contract) noexcept -> void;

private:
    explicit DeclarationDefinitionSink(DeclarationSessionState& state) noexcept;

    DeclarationSessionState* session_state;

    friend class DeclarationSessionState;
};

class ResolvedDeclarations final {
public:
    ResolvedDeclarations(const ResolvedDeclarations&) = delete;
    ResolvedDeclarations(ResolvedDeclarations&& other) noexcept;
    ~ResolvedDeclarations() noexcept;

    auto operator=(const ResolvedDeclarations&) -> ResolvedDeclarations& = delete;
    auto operator=(ResolvedDeclarations&& other) noexcept -> ResolvedDeclarations&;

    auto view() const noexcept -> DeclarationSessionView;
    auto finish() && noexcept -> void;

private:
    explicit ResolvedDeclarations(std::unique_ptr<DeclarationSessionState> state) noexcept;

    std::unique_ptr<DeclarationSessionState> session_state;

    friend class DeclarationConstruction;
};

class DeclarationConstruction final {
public:
    DeclarationConstruction(AnalysisCatalogView catalog, SemanticConstruction& builder) noexcept;
    DeclarationConstruction(const DeclarationConstruction&) = delete;
    DeclarationConstruction(DeclarationConstruction&& other) noexcept;
    ~DeclarationConstruction() noexcept;

    auto operator=(const DeclarationConstruction&) -> DeclarationConstruction& = delete;
    auto operator=(DeclarationConstruction&& other) noexcept -> DeclarationConstruction&;

    auto view() noexcept -> DeclarationSessionView;
    auto resolve_all(std::span<ModuleAnalysis> modules) && noexcept -> ResolvedDeclarations;

private:
    std::unique_ptr<DeclarationSessionState> session_state;
};
