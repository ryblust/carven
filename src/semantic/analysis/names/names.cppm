module carven:semantic.analysis.names;

import :diagnostics.code;
import :frontend.ast.ids;
import :frontend.ast.storage;
import :frontend.ast.type;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.analysis.source.builder;
import :semantic.semir.program;
import :source.text;
import std;

struct SourceNamePath final {
    std::optional<Span> global_root;
    std::vector<Span> components;
};

auto source_name_path(const ASTNamedType& type) noexcept -> SourceNamePath;

auto source_name_path(ASTView syntax, ASTExprID expression) noexcept
    -> std::optional<SourceNamePath>;

auto lookup_cpp_name(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID source_module,
    const SourceNamePath& path
) noexcept -> AnalysisResult<std::optional<CppNameReference>>;

auto lookup_class_operation(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ProgramModuleID source_module,
    ConstructionStructRef owner,
    std::optional<ClassDeclarationRef> lexical_class,
    std::string_view name,
    bool receiver,
    Span span
) noexcept -> AnalysisResult<CatalogSymbolID>;

auto check_class_representation_access(
    const ProgramDraft& draft,
    ProgramModuleID source_module,
    ConstructionStructRef owner,
    std::optional<ClassDeclarationRef> lexical_class,
    Span span
) noexcept -> AnalysisResult<void>;

auto select_catalog_entity(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module_id,
    std::string_view name,
    Span origin,
    SourceObservation* observations,
    DiagnosticCode unresolved
) noexcept -> AnalysisResult<CatalogEntity>;
