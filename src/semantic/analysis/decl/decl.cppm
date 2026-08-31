module carven:semantic.analysis.decl;

import :semantic.analysis.session;
import :semantic.analysis.catalog;
import :semantic.hir.decl;
import :semantic.hir.ids;
import :source.text;
import std;

class ModuleAnalysis;
class DeclarationResolverState;

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

class DeclarationContractView final {
public:
    auto function(FunctionID id) const noexcept -> FunctionContractView;
    auto structure(StructID id) const noexcept -> StructContractView;
    auto enumeration(EnumID id) const noexcept -> EnumContractView;
    auto enum_case(EnumCaseID id) const noexcept -> SemanticEnumCaseView;
    auto enum_count() const noexcept -> std::size_t;
    auto enum_case_count() const noexcept -> std::size_t;
    auto supports_equality(HIRTypeID id) const noexcept -> bool;
    auto resolve(SymbolID symbol, ProgramModuleID requester_module, Span origin) const noexcept
        -> bool;

private:
    explicit DeclarationContractView(DeclarationResolverState& state) noexcept;

    DeclarationResolverState* state;

    friend class DeclarationResolver;
};

class DeclarationDefinitionSink final {
public:
    auto define(FunctionID id, HIRFunctionDecl declaration) noexcept -> void;
    auto define(StructID id, HIRStructDecl declaration) noexcept -> void;
    auto define(EnumID id, HIREnumDecl declaration) noexcept -> void;
    auto define(EnumCaseID id, SemanticEnumCaseContract declaration) noexcept -> void;

private:
    explicit DeclarationDefinitionSink(DeclarationResolverState& state) noexcept;

    DeclarationResolverState* state;

    friend class DeclarationResolverState;
};

class DeclarationResolver final {
public:
    DeclarationResolver(
        AnalysisCatalogView catalog,
        SemanticDeclarationCapabilities capabilities
    ) noexcept;
    DeclarationResolver(const DeclarationResolver&) = delete;
    DeclarationResolver(DeclarationResolver&&) = delete;
    ~DeclarationResolver() noexcept;

    auto operator=(const DeclarationResolver&) -> DeclarationResolver& = delete;
    auto operator=(DeclarationResolver&&) -> DeclarationResolver& = delete;

    auto view() noexcept -> DeclarationContractView;
    auto resolve_all(std::span<ModuleAnalysis> modules) noexcept -> void;

private:
    std::unique_ptr<DeclarationResolverState> state;
};
