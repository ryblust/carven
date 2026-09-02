module carven:semantic.analysis.catalog;

import :diagnostics.diagnostic;
import :frontend.ast.ids;
import :frontend.ast.tree;
import :semantic.analysis.session;
import :semantic.hir.ids;
import :semantic.visibility;
import :source.provenance;
import :source.text;
import :support.id_table;
import :support.typed_id;
import std;

struct ImportBindingIDTag final {};
using ImportBindingID = TypedID<ImportBindingIDTag>;

struct CatalogFunctionForm final {
    FunctionID function;
};

struct CatalogStructForm final {
    StructID structure;
};

struct CatalogEnumForm final {
    EnumID enumeration;
    std::vector<EnumCaseID> cases;
};

struct CatalogEnumCaseForm final {
    EnumCaseID enum_case;
    EnumID owner;
    std::uint32_t index;
};

struct CatalogConstantForm final {};

using CatalogSymbolForm = std::variant<
    CatalogFunctionForm,
    CatalogStructForm,
    CatalogEnumForm,
    CatalogEnumCaseForm,
    CatalogConstantForm>;

struct CatalogSymbol final {
    SymbolID symbol_id;
    ProgramModuleID module_id;
    ASTItemID item_id;
    std::string name;
    CatalogSymbolForm form;
    DeclarationVisibility visibility;
    Span declaration_span;
};

struct CatalogTestForm final {};

using CatalogModuleItemForm = std::variant<FunctionID, StructID, EnumID, CatalogTestForm>;

struct CatalogModuleItem final {
    ASTItemID item_id;
    CatalogModuleItemForm form;
};

struct CatalogModule final {
    ProgramModuleID module_id;
    std::vector<SymbolID> symbols;
    std::vector<CatalogModuleItem> items;
};

enum class CatalogImportSelectionKind {
    Single,
    List,
    Wildcard,
};

struct CatalogImportSelectedSymbol final {
    SymbolID symbol_id;
    std::string name;
    Span origin;
};

struct CatalogImportBinding final {
    ImportBindingID binding_id;
    ProgramModuleID importer;
    ASTModuleImportID declaration_id;
    ProgramModuleID target;
    Span declaration_span;
    Span reference_span;
    Span selection_span;
    CatalogImportSelectionKind selection_kind;
    std::vector<CatalogImportSelectedSymbol> selected_symbols;
    bool used;
};

struct CatalogLookupCandidate final {
    SymbolID symbol_id;
    std::optional<ImportBindingID> import_binding;
};

class AnalysisCatalogView;

class AnalysisCatalog final {
public:
    AnalysisCatalog(const AnalysisCatalog&) = delete;
    AnalysisCatalog(AnalysisCatalog&& other) = default;
    ~AnalysisCatalog() = default;

    auto operator=(const AnalysisCatalog&) -> AnalysisCatalog& = delete;
    auto operator=(AnalysisCatalog&& other) -> AnalysisCatalog& = default;

    auto view() const noexcept -> AnalysisCatalogView;

private:
    explicit AnalysisCatalog(CompilationProvenanceView provenance) noexcept;

    CompilationProvenanceView compilation_provenance;
    std::vector<CatalogModule> modules;
    std::vector<CatalogSymbol> symbols;
    std::vector<SymbolID> function_symbols;
    std::vector<SymbolID> struct_symbols;
    std::vector<SymbolID> enum_symbols;
    std::vector<SymbolID> enum_case_symbols;
    std::vector<std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>>
        visible_candidates;
    mutable std::vector<CatalogImportBinding> import_bindings;

    friend class AnalysisCatalogView;
    friend auto build_analysis_catalog(
        CompilationProvenanceView,
        std::span<const SyntaxTree>,
        SemanticEntityReservations
    ) noexcept -> std::expected<AnalysisCatalog, Diagnostics>;
};

class AnalysisCatalogView final {
public:
    auto modules() const noexcept -> std::span<const CatalogModule>;
    auto symbols() const noexcept -> std::span<const CatalogSymbol>;
    auto imports() const noexcept -> std::span<const CatalogImportBinding>;
    auto module_record(ProgramModuleID module_id) const noexcept -> const ProgramModule&;
    auto find_module(ProgramModuleID module_id) const noexcept -> const CatalogModule*;
    auto symbol(SymbolID id) const noexcept -> const CatalogSymbol*;
    auto function_symbol(FunctionID id) const noexcept -> SymbolID;
    auto struct_symbol(StructID id) const noexcept -> SymbolID;
    auto enum_symbol(EnumID id) const noexcept -> SymbolID;
    auto enum_case_symbol(EnumCaseID id) const noexcept -> SymbolID;
    auto function_count() const noexcept -> std::size_t;
    auto struct_count() const noexcept -> std::size_t;
    auto enum_count() const noexcept -> std::size_t;
    auto enum_case_count() const noexcept -> std::size_t;
    auto lookup(ProgramModuleID module_id, std::string_view name) const noexcept
        -> std::span<const CatalogLookupCandidate>;
    auto mark_import_used(ImportBindingID binding) const noexcept -> void;

private:
    explicit AnalysisCatalogView(const AnalysisCatalog& catalog) noexcept;

    const AnalysisCatalog* catalog;

    friend class AnalysisCatalog;
};

auto build_analysis_catalog(
    CompilationProvenanceView provenance,
    std::span<const SyntaxTree> syntax_trees,
    SemanticEntityReservations reservations
) noexcept -> std::expected<AnalysisCatalog, Diagnostics>;
