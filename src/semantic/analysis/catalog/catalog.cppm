module carven:semantic.analysis.catalog;

import :diagnostics.diagnostic;
import :frontend.ast.ids;
import :frontend.ast.tree;
import :frontend.program;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.visibility;
import :source.text;
import :support.typed_id;
import std;

struct CatalogSymbolIDTag final {};
using CatalogSymbolID = TypedID<CatalogSymbolIDTag>;

struct ImportBindingIDTag final {};
using ImportBindingID = TypedID<ImportBindingIDTag>;

struct CatalogFunctionForm final {
    FunctionID function;
    CallableID callable;
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

struct CatalogConstantForm final {
    ModuleConstantID constant;
};

using CatalogSymbolForm = std::variant<
    CatalogFunctionForm,
    CatalogStructForm,
    CatalogEnumForm,
    CatalogEnumCaseForm,
    CatalogConstantForm>;

struct CatalogSymbol final {
    CatalogSymbolID symbol_id;
    ProgramModuleID module_id;
    ASTItemID item_id;
    std::string name;
    CatalogSymbolForm form;
    DeclarationVisibility visibility;
    Span declaration_span;
};

struct CatalogTestForm final {
    TestID test;
};

using CatalogModuleItemForm =
    std::variant<FunctionID, StructID, EnumID, ModuleConstantID, CatalogTestForm>;

struct CatalogModuleItem final {
    ASTItemID item_id;
    CatalogModuleItemForm form;
};

struct CatalogModule final {
    ProgramModuleID module_id;
    ModuleID declaration;
    std::vector<CatalogSymbolID> symbols;
    std::vector<CatalogModuleItem> items;
};

enum class CatalogImportSelectionKind {
    Single,
    List,
    Wildcard,
};

struct CatalogImportSelectedSymbol final {
    CatalogSymbolID symbol_id;
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
};

struct CatalogCppBinding final {
    std::vector<std::string> components;
    bool opens_namespace;
    Span origin;
};

struct CatalogLookupCandidate final {
    CatalogSymbolID symbol_id;
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
    AnalysisCatalog() = default;
    std::vector<CatalogModule> modules;
    std::vector<CatalogSymbol> symbols;
    std::vector<CatalogSymbolID> function_symbols;
    std::vector<CatalogSymbolID> struct_symbols;
    std::vector<CatalogSymbolID> enum_symbols;
    std::vector<CatalogSymbolID> enum_case_symbols;
    std::vector<CatalogSymbolID> module_constant_symbols;
    std::vector<std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>>
        visible_candidates;
    std::vector<CatalogImportBinding> import_bindings;
    std::vector<std::vector<CatalogCppBinding>> cpp_bindings;

    friend class AnalysisCatalogView;
    friend auto build_analysis_catalog(ProgramDraft&) noexcept
        -> std::expected<AnalysisCatalog, Diagnostics>;
};

class AnalysisCatalogView final {
public:
    auto modules() const noexcept -> std::span<const CatalogModule>;
    auto symbols() const noexcept -> std::span<const CatalogSymbol>;
    auto imports() const noexcept -> std::span<const CatalogImportBinding>;
    auto find_module(ProgramModuleID module_id) const noexcept -> const CatalogModule*;
    auto symbol(CatalogSymbolID id) const noexcept -> const CatalogSymbol*;
    auto function_symbol(FunctionID id) const noexcept -> CatalogSymbolID;
    auto struct_symbol(StructID id) const noexcept -> CatalogSymbolID;
    auto enum_symbol(EnumID id) const noexcept -> CatalogSymbolID;
    auto enum_case_symbol(EnumCaseID id) const noexcept -> CatalogSymbolID;
    auto module_constant_symbol(ModuleConstantID id) const noexcept -> CatalogSymbolID;
    auto function_count() const noexcept -> std::size_t;
    auto struct_count() const noexcept -> std::size_t;
    auto enum_count() const noexcept -> std::size_t;
    auto enum_case_count() const noexcept -> std::size_t;
    auto cpp_imports(ProgramModuleID module_id) const noexcept
        -> std::span<const CatalogCppBinding>;
    auto lookup(ProgramModuleID module_id, std::string_view name) const noexcept
        -> std::span<const CatalogLookupCandidate>;

private:
    explicit AnalysisCatalogView(const AnalysisCatalog& catalog) noexcept;

    const AnalysisCatalog* catalog;

    friend class AnalysisCatalog;
};

auto build_analysis_catalog(ProgramDraft& draft) noexcept
    -> std::expected<AnalysisCatalog, Diagnostics>;

class ImportUsage final {
public:
    explicit ImportUsage(std::size_t import_count) noexcept;
    auto record(ImportBindingID import_id) noexcept -> void;
    auto record_cpp(ProgramModuleID module_id, Span origin) noexcept -> void;
    auto cpp_was_used(ProgramModuleID module_id, Span origin) const noexcept -> bool;
    auto was_used(ImportBindingID import_id) const noexcept -> bool;

private:
    std::vector<std::uint8_t> used_imports;
    std::flat_set<std::pair<ProgramModuleID, std::uint32_t>> used_cpp_imports;
};
