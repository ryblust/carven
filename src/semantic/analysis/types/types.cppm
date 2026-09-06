module carven:semantic.analysis.types;

import :frontend.ast.ids;
import :frontend.ast.pattern;
import :frontend.ast.storage;
import :frontend.ast.type;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;
import std;

using ArrayExtentResolver = std::function<AnalysisResult<std::uint64_t>(ASTExprID)>;

auto semantic_access_mode(ASTAccessSyntax access) noexcept -> AccessMode;

auto resolve_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef>;

auto resolve_source_construction_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstructionType& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef>;

auto resolve_source_constraint_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstraintOperand& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef>;

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
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<std::vector<TypeID>>;
