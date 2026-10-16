module carven:semantic.analysis.decl.resolver;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.construction.requests;
import :semantic.analysis.decl.context;
import :semantic.analysis.diagnostics;
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
        ImportUsage& usage,
        ConstructionRequests& requests
    ) noexcept;

    auto run() noexcept -> AnalysisTask<void>;

    auto ensure_available(CatalogSymbolID id, ProgramModuleID requester, Span origin) noexcept
        -> AnalysisTask<void>;

    auto prepare_type(ConstructionTypeRef type, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

    auto prepare_callable(CallableID callable, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

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
        -> AnalysisTask<void>;

    auto resolve_fresh(const CatalogSymbol& symbol) noexcept -> AnalysisTask<void>;

    struct ConstantScope final {
        DeclResolver& resolver;
        ProgramModuleID module_id;
        ASTView syntax;
        const GenericTypeContext* generic_context = nullptr;

        auto evaluate_constant_expression(
            ASTExprID expression,
            std::optional<ConstructionTypeRef> expected = std::nullopt
        ) noexcept -> AnalysisTask<std::optional<ConstantID>>;

        auto evaluate_array_extent(ASTExprID expression) noexcept -> AnalysisTask<std::uint64_t>;
    };

    auto resolve_type(ProgramModuleID module_id, ASTView syntax, ASTTypeID type) noexcept
        -> AnalysisTask<ConstructionTypeRef>;

    auto resolve_failures(
        ProgramModuleID module_id,
        ASTView syntax,
        const ASTThrowClause& clause,
        const GenericTypeContext* generic_context = nullptr
    ) noexcept -> AnalysisTask<ConstructionFailureRef>;

    auto resolve_function(
        const CatalogSymbol& symbol,
        const CatalogFunctionForm& form,
        ASTView syntax,
        const ASTFunctionDecl& function,
        Span item_span
    ) noexcept -> AnalysisTask<void>;

    auto resolve_generic_declaration(
        const CatalogSymbol& symbol,
        const CatalogGenericForm& form
    ) noexcept -> AnalysisTask<void>;

    auto resolve_struct(
        const CatalogSymbol& symbol,
        const CatalogStructForm& form,
        ASTView syntax,
        const ASTRecordDecl& structure,
        Span item_span
    ) noexcept -> AnalysisTask<void>;

    auto resolve_enum(
        const CatalogSymbol& symbol,
        const CatalogEnumForm& form,
        ASTView syntax,
        const ASTEnumDecl& enumeration,
        Span item_span
    ) noexcept -> AnalysisTask<void>;

    auto resolve_enum_case(const CatalogSymbol& symbol, const CatalogEnumCaseForm& form) noexcept
        -> AnalysisTask<void>;

    auto resolve_module_constant(
        const CatalogSymbol& symbol,
        const CatalogConstantForm& form,
        ASTView syntax,
        const ASTConstantDecl& declaration,
        Span item_span
    ) noexcept -> AnalysisTask<void>;

    auto validate_enum_codes(const CatalogSymbol& symbol) noexcept -> AnalysisResult<void>;
    auto publish_modules() noexcept -> void;
    auto finish_declarations() noexcept -> void;
    auto publish_declaration(const CatalogSymbol& symbol) noexcept -> AnalysisResult<void>;

    auto prepare_type_dependencies(
        ConstructionTypeRef type,
        ProgramModuleID requester,
        Span span,
        std::flat_set<ConstructionTypeRef>& visiting,
        std::vector<CatalogSymbolID>& prepared
    ) noexcept -> AnalysisTask<void>;

    ProgramDraft& draft;
    AnalysisCatalogView catalog;
    ImportUsage& import_usage;
    ConstructionRequests& requests;
    bool heads_finished;
    std::vector<State> states;
    std::vector<bool> published;
    std::vector<CatalogSymbolID> active_path;
    std::vector<std::optional<ConstructionStructDeclaration>> structures;
    std::vector<std::optional<EnumDeclaration>> enumerations;
    std::vector<std::optional<ConstructionEnumCaseDeclaration>> enum_cases;
    std::vector<std::optional<ModuleConstantDeclaration>> module_constants;
    std::vector<std::optional<CppImportImplementation>> cpp_imports;
};
