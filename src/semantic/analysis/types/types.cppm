module carven:semantic.analysis.types;

import :frontend.ast.ids;
import :frontend.ast.pattern;
import :frontend.ast.storage;
import :frontend.ast.type;
import :semantic.analysis.catalog;
import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.generic;
import :semantic.semir.program;
import :support.function_ref;
import std;

auto source_builtin_type(std::string_view name) noexcept -> std::optional<BuiltinType>;
auto source_type_name_is_reserved(std::string_view name) noexcept -> bool;

// The callable outlives each returned task, including nested type resolution.
using ArrayExtentResolver = FunctionRef<AnalysisTask<std::uint64_t>(ASTExprID) noexcept>;

auto semantic_access_mode(ASTAccessSyntax access) noexcept -> AccessMode;

struct GenericTypeContext final {
    GenericDeclarationID definition;
    std::span<const ProgramSpellingID> parameters;
    std::span<const GenericParameterKind> parameter_kinds;
};

auto resolve_source_generic_argument(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTGenericArgument& argument,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr,
    std::optional<GenericParameterKind> expected = std::nullopt
) noexcept -> AnalysisTask<GenericArgument>;

auto resolve_generic_source_arguments(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    std::span<const ASTGenericArgument> arguments,
    std::span<const GenericParameterKind> policies,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr
) noexcept -> AnalysisTask<GenericEnvironment>;

auto resolve_generic_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    GenericTypeContext context,
    ArrayExtentResolver resolve_extent,
    bool value_required = true,
    ConstructionRequests* requests = nullptr
) noexcept -> AnalysisTask<GenericTypeID>;

auto resolve_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr
) noexcept -> AnalysisTask<ConstructionTypeRef>;

auto resolve_source_type_application(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTTypeApplicationExpr& application,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr
) noexcept -> AnalysisTask<ConstructionTypeRef>;

auto resolve_source_construction_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstructionType& source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr
) noexcept -> AnalysisTask<ConstructionTypeRef>;

auto resolve_source_constraint_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstraintOperand& source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr
) noexcept -> AnalysisTask<ConstructionTypeRef>;

auto require_source_value_type(
    const ProgramDraft& draft,
    ConstructionTypeRef type,
    ProgramModuleID module_id,
    Span origin,
    std::string_view role
) noexcept -> AnalysisResult<ConstructionTypeRef>;

auto resolve_failure_types(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTThrowClause& clause,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests = nullptr,
    const GenericTypeContext* generic_context = nullptr
) noexcept -> AnalysisTask<ConstructionFailureRef>;
