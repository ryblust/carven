module carven:semantic.analysis.construction.requests;

import :frontend.ast.ids;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.semir.generic;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.standard;
import :source.text;

struct GenericTypeContext;
class ProgramConstruction;
class StaticStage;

// Borrowed completion port. Its coordinator outlives every request and continuation.
class ConstructionRequests final {
public:
    auto standard_type(
        StandardDeclaration declaration,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisTask<TypeID>;
    auto number_parser(BuiltinType number, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<CallableID>;
    auto standard_function(
        StandardDeclaration function,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisTask<FunctionID>;

    auto ensure_declaration(CatalogSymbolID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

    auto evaluate_constant_expression(
        ProgramModuleID module_id,
        ASTExprID expression,
        std::optional<ConstructionTypeRef> expected,
        const GenericTypeContext* generic_context
    ) noexcept -> AnalysisTask<std::optional<ConstantID>>;

    auto ensure_generic_function(
        GenericDeclarationID id,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisTask<void>;

    auto ensure_function_signature(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

    auto ensure_function_body(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<BodyID>;

    auto ensure_type(ConstructionTypeRef type, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

    auto ensure_callable(CallableID callable, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;

    auto close_generic_environment(
        std::span<const GenericArgument> arguments,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisTask<CanonicalGenericEnvironment>;

    auto close_generic_calls(SemanticRegion& region) noexcept -> AnalysisTask<bool>;
    auto stage() noexcept -> StaticStage&;

private:
    auto standard_declaration(
        StandardDeclaration declaration,
        ProgramModuleID requester,
        Span span
    ) noexcept -> AnalysisTask<CatalogSymbolID>;

    explicit ConstructionRequests(ProgramConstruction& owner) noexcept;
    ProgramConstruction& owner;

    friend class ProgramConstruction;
};
