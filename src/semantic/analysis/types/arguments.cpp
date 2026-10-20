module carven:semantic.analysis.types.arguments.impl;

import :diagnostics.builder;
import :semantic.analysis.types;
import :semantic.analysis.names;
import std;

namespace {

auto argument_error(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    Span span,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(
        DiagnosticBuilder(DiagnosticCode::TypeGenericArguments, std::move(message))
            .primary(locate(draft.module_source_id(module_id), span))
            .build()
    );
}

} // namespace

auto resolve_source_capability(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID bound,
    const GenericTypeContext* context
) noexcept -> AnalysisResult<BuiltinCapability> {
    const auto& type = syntax.type(bound);
    const auto* name = std::get_if<ASTNamedType>(&type.value);
    if (name && !name->global_root && name->components.size() == 1uz && name->arguments.empty()) {
        const auto span = name->components.front().name_span;
        const auto spelling = draft.source_slice_copy(module_id, span);
        if (generic_parameter_index(draft, context, spelling)) {
            return std::unexpected(argument_error(
                draft,
                module_id,
                span,
                "a type parameter does not name a capability"
            ));
        }
        auto selected = select_catalog_entity(
            draft,
            catalog,
            usage,
            module_id,
            spelling,
            span,
            nullptr,
            DiagnosticCode::TypeGenericDefinition
        );
        if (!selected) {
            return std::unexpected(selected.error());
        }
        if (const auto* provided = std::get_if<ProvidedExport>(&*selected)) {
            if (*provided == ProvidedExport::Hash) {
                return BuiltinCapability::Hash;
            }
            if (*provided == ProvidedExport::Equal) {
                return BuiltinCapability::Equal;
            }
        }
    }
    return std::unexpected(
        argument_error(draft, module_id, type.span, "generic bound must name a capability")
    );
}

auto resolve_source_generic_argument(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTGenericArgument& argument,
    ArrayExtentResolver extent,
    ConstructionRequests* requests,
    const GenericTypeContext* context,
    std::optional<GenericParameterKind> expected,
    SourceObservation* observations
) noexcept -> AnalysisTask<GenericArgument> {
    auto result = std::optional<GenericArgument>();
    if (const auto* type = std::get_if<ASTTypeID>(&argument.value)) {
        auto resolved = co_await resolve_source_type(
            draft,
            catalog,
            usage,
            module_id,
            syntax,
            *type,
            extent,
            requests,
            context,
            observations
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        result = *resolved;
    } else {
        auto resolved = co_await resolve_failure_types(
            draft,
            catalog,
            usage,
            module_id,
            syntax,
            std::get<ASTThrowClause>(argument.value),
            extent,
            requests,
            context,
            observations
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        result = *resolved;
    }
    if (expected && !generic_argument_matches(*expected, *result)) {
        co_return std::unexpected(argument_error(
            draft,
            module_id,
            argument.span,
            "generic argument kind differs from its parameter"
        ));
    }
    co_return *result;
}

auto resolve_generic_source_arguments(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module_id,
    ASTView syntax,
    std::span<const ASTGenericArgument> arguments,
    std::span<const GenericParameterKind> policies,
    ArrayExtentResolver extent,
    ConstructionRequests* requests,
    const GenericTypeContext* context,
    SourceObservation* observations
) noexcept -> AnalysisTask<GenericEnvironment> {
    if (arguments.size() != policies.size()) {
        const auto span = arguments.empty() ? Span::at(0u) : arguments.front().span;
        co_return std::unexpected(argument_error(
            draft,
            module_id,
            span,
            "generic argument count differs from its declaration"
        ));
    }
    auto result = GenericEnvironment();
    for (auto index = 0uz; index < arguments.size(); ++index) {
        auto resolved = co_await resolve_source_generic_argument(
            draft,
            catalog,
            usage,
            module_id,
            syntax,
            arguments[index],
            extent,
            requests,
            context,
            policies[index],
            observations
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        result.push_back(*resolved);
    }
    co_return result;
}
