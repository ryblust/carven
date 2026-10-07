module carven:semantic.analysis.types;

import :frontend.ast.ids;
import :frontend.ast.pattern;
import :frontend.ast.storage;
import :frontend.ast.type;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.analysis.source.builder;
import :semantic.semir.program;
import :support.function_ref;
import std;

auto source_builtin_type(std::string_view name) noexcept -> std::optional<BuiltinType>;

// The callable outlives each returned task, including nested type resolution.
using ArrayExtentResolver = FunctionRef<AnalysisTask<std::uint64_t>(ASTExprID) noexcept>;

auto semantic_access_mode(ASTAccessSyntax access) noexcept -> AccessMode;

auto resolve_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    ArrayExtentResolver resolve_extent,
    SourceObservation* observations = nullptr
) noexcept -> AnalysisTask<ConstructionTypeRef>;

auto resolve_source_construction_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstructionType& source_type,
    ArrayExtentResolver resolve_extent,
    SourceObservation* observations = nullptr
) noexcept -> AnalysisTask<ConstructionTypeRef>;

auto resolve_source_constraint_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstraintOperand& source_type,
    ArrayExtentResolver resolve_extent,
    SourceObservation* observations = nullptr
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
    SourceObservation* observations = nullptr
) noexcept -> AnalysisTask<std::vector<TypeID>>;
