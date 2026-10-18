module carven:semantic.analysis.names;

import :diagnostics.code;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.analysis.source.builder;
import :semantic.semir.program;
import :source.text;
import std;

auto lookup_cpp_name(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID source_module,
    CppNameLookup lookup,
    std::span<const Span> components
) noexcept -> AnalysisResult<std::optional<CppNameReference>>;

auto lookup_class_operation(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ProgramModuleID source_module,
    StructID owner,
    std::optional<StructID> lexical_class,
    std::string_view name,
    bool receiver,
    Span span
) noexcept -> AnalysisResult<FunctionID>;

auto check_class_representation_access(
    const ProgramDraft& draft,
    ProgramModuleID source_module,
    StructID owner,
    std::optional<StructID> lexical_class,
    Span span
) noexcept -> AnalysisResult<void>;

auto select_catalog_entity(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module,
    std::string_view name,
    Span origin,
    SourceObservation* observations,
    DiagnosticCode unresolved
) noexcept -> AnalysisResult<CatalogEntity>;
