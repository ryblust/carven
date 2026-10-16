module carven:semantic.analysis.types.arguments.impl;

import :diagnostics.builder;
import :semantic.analysis.types;
import std;

namespace {

auto argument_error(
    ProgramDraft& draft,
    ProgramModuleID module,
    Span span,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(
        DiagnosticBuilder(DiagnosticCode::TypeGenericArguments, std::move(message))
            .primary(locate(draft.module_source_id(module), span))
            .build()
    );
}

} // namespace

auto resolve_source_generic_argument(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module,
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
            module,
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
            module,
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
            module,
            argument.span,
            "generic argument kind differs from its parameter"
        ));
    }
    co_return std::move(*result);
}

auto resolve_generic_source_arguments(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module,
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
            module,
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
            module,
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
        result.push_back(std::move(*resolved));
    }
    co_return result;
}
