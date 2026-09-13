module carven:semantic.analysis.decl.resolver;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.decl.context;
import :semantic.analysis.expr.constant;
import :semantic.analysis.expr.scope;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

class DeclResolver final {
public:
    DeclResolver(
        ProgramDraft& target,
        AnalysisCatalogView source_catalog,
        ImportUsage& usage
    ) noexcept;
    auto run() noexcept -> AnalysisResult<void>;

private:
    struct Unvisited final {};

    struct Resolving final {};

    struct Resolved final {};

    struct Failed final {
        AnalysisFailure failure;
    };

    using State = std::variant<Unvisited, Resolving, Resolved, Failed>;

    auto module_declaration(ProgramModuleID id) const noexcept -> ModuleID;
    auto diagnose_cycle(
        const CatalogSymbol& target,
        ProgramModuleID requester,
        Span origin
    ) noexcept -> AnalysisFailure;
    auto resolve(CatalogSymbolID id, ProgramModuleID requester, Span origin) noexcept
        -> AnalysisResult<void>;
    auto resolve_fresh(const CatalogSymbol& symbol) noexcept -> AnalysisResult<void>;
    auto select_symbol(ProgramModuleID module_id, std::string_view name, Span origin) noexcept
        -> AnalysisResult<const CatalogSymbol*>;

    struct ConstantScope final {
        DeclResolver& resolver;
        ProgramModuleID module;
        ASTView syntax;

        auto resolve_name(std::string_view name, Span span) noexcept
            -> AnalysisResult<ResolvedConstantName>;
        auto resolve_enum_qualifier(ASTExprID expression) noexcept
            -> AnalysisResult<std::optional<TypeID>>;
        auto resolve_enum_case(TypeID type, std::string_view name, Span span) noexcept
            -> AnalysisResult<ResolvedEnumCase>;
        auto resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef>;
        auto supports_equality(ConstructionTypeRef type) noexcept -> bool;
        auto is_numeric_enum(TypeID type) const noexcept -> bool;
    };

    auto resolve_type(ProgramModuleID module_id, ASTView syntax, ASTTypeID type) noexcept
        -> AnalysisResult<ConstructionTypeRef>;
    auto resolve_value_type(
        ProgramModuleID module_id,
        ASTView syntax,
        ASTTypeID type,
        std::string_view role
    ) noexcept -> AnalysisResult<ConstructionTypeRef>;
    auto resolve_failures(
        ProgramModuleID module_id,
        ASTView syntax,
        const ASTThrowClause& clause
    ) noexcept -> AnalysisResult<std::vector<TypeID>>;
    auto resolve_function(
        const CatalogSymbol& symbol,
        const CatalogFunctionForm& form,
        ASTView syntax,
        const ASTFunctionDecl& function,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_struct(
        const CatalogSymbol& symbol,
        const CatalogStructForm& form,
        ASTView syntax,
        const ASTStructDecl& structure,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_enum(
        const CatalogSymbol& symbol,
        const CatalogEnumForm& form,
        ASTView syntax,
        const ASTEnumDecl& enumeration,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_enum_case(const CatalogSymbol& symbol, const CatalogEnumCaseForm& form) noexcept
        -> AnalysisResult<void>;
    auto resolve_module_constant(
        const CatalogSymbol& symbol,
        const CatalogConstantForm& form,
        ASTView syntax,
        const ASTConstantDecl& declaration,
        Span item_span
    ) noexcept -> AnalysisResult<void>;
    auto resolve_constant_name(
        ProgramModuleID module_id,
        std::string_view name,
        Span origin
    ) noexcept -> AnalysisResult<ResolvedConstantName>;
    auto resolve_enum_qualifier(
        ProgramModuleID module_id,
        ASTView syntax,
        ASTExprID expression
    ) noexcept -> AnalysisResult<std::optional<TypeID>>;
    auto resolve_constant_enum_case(
        ProgramModuleID module_id,
        TypeID type,
        std::string_view name,
        Span origin
    ) noexcept -> AnalysisResult<ResolvedEnumCase>;
    auto supports_equality(ConstructionTypeRef type, std::flat_set<TypeID>& visiting) noexcept
        -> bool;
    auto validate_enum_codes(const CatalogSymbol& symbol) noexcept -> AnalysisResult<void>;
    auto finish_capabilities() noexcept -> void;
    auto finish_declarations() noexcept -> void;

    ProgramDraft& draft;
    AnalysisCatalogView catalog;
    ImportUsage& import_usage;
    std::vector<State> states;
    std::vector<CatalogSymbolID> active_path;
    std::vector<std::optional<ConstructionStructDeclaration>> structures;
    std::vector<std::optional<EnumDeclaration>> enumerations;
    std::vector<std::optional<ConstructionEnumCaseDeclaration>> enum_cases;
    std::vector<std::optional<ModuleConstantDeclaration>> module_constants;
    std::vector<std::optional<ProgramOriginID>> cpp_import_origins;
};
