module carven:semantic.analysis.constant.evaluation.impl;

import :diagnostics.builder;
import :semantic.analysis.constant.evaluation;
import :semantic.evaluation.execution;
import :semantic.semir.decl;
import std;

namespace {

class ConstantAnalysisContext final : public SemanticExecutionContext {
public:
    ConstantAnalysisContext(
        ProgramDraft& draft,
        ConstructionRequests& requests,
        std::optional<ConstantBodyRoot> root = std::nullopt
    ) noexcept;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto prepare_call(FunctionID function, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> override;
    auto report(const ExecutionDiagnostic& diagnostic) noexcept -> void override;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;

private:
    std::map<ProgramSourceID, ProgramModuleID> modules;
    std::map<FunctionID, BodyID> completed_calls;
    ProgramDraft& draft;
    ConstructionRequests& requests;
    std::optional<ConstantBodyRoot> root;
};

ConstantAnalysisContext::ConstantAnalysisContext(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    std::optional<ConstantBodyRoot> root
) noexcept
    : draft(draft),
      requests(requests),
      root(root) {}

auto ConstantAnalysisContext::write(ExecutionOutputStream stream, std::string_view bytes) noexcept
    -> void {
    draft.write_output(stream, bytes);
}

auto ConstantAnalysisContext::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    return draft.function_for_callable(callable);
}

auto ConstantAnalysisContext::prepare_call(FunctionID function, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> {
    const auto declaration = draft.function_declaration_copy(function);
    if (!declaration.is_const) {
        co_return std::unexpected(
            ExecutionDiagnostic {
                .origin = origin,
                .code = DiagnosticCode::ConstAdmission,
                .message =
                    "required constant expression can only call an explicitly declared const fn",
                .calls = {},
                .report_kind = std::nullopt,
            }
        );
    }
    if (const auto found = completed_calls.find(function); found != completed_calls.end()) {
        co_return ExecutionBody(draft.body_draft(found->second));
    }
    if (modules.empty()) {
        for (auto index = 0uz; index < draft.module_count(); ++index) {
            const auto module_id = draft.provenance_module_at(index);
            modules.emplace(draft.module_source(module_id), module_id);
        }
    }
    const auto location = draft.source_origin(origin);
    const auto requester = modules.at(location.source_id);
    auto completed = co_await requests.ensure_function_body(function, requester, location.span);
    if (!completed) {
        co_return std::unexpected(ExecutionDependencyFailure {});
    }
    completed_calls.emplace(function, *completed);
    co_return ExecutionBody(draft.body_draft(*completed));
}

auto ConstantAnalysisContext::report(const ExecutionDiagnostic& failure) noexcept -> void {
    auto diagnostic = DiagnosticBuilder(failure.code, failure.message);
    diagnostic.primary(draft.source_span(failure.origin));
    auto shown = 0uz;
    for (const auto call : failure.calls | std::views::reverse) {
        if (call == failure.origin) {
            continue;
        }
        diagnostic.related(draft.source_span(call), "while evaluating this const function call");
        if (++shown == 8uz) {
            break;
        }
    }
    if (root) {
        const auto kind =
            draft.body_draft(root->body).kind == BodyKind::Test ? "const test" : "constant block";
        auto message = std::format("while evaluating this {}", kind);
        if (root->source.label) {
            message += std::format(" {:?}", draft.spelling(*root->source.label));
        }
        diagnostic.related(draft.source_span(root->source.origin), std::move(message));
    }
    static_cast<void>(draft.diagnostics().error(diagnostic.build()));
}

} // namespace

auto evaluate_constant_root(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    const SemanticExpression& expression
) noexcept -> AnalysisTask<ExecutionValue> {
    auto context = ConstantAnalysisContext(draft, requests);
    auto evaluated = (co_await execute_constant_root(draft, context, expression));
    if (evaluated) {
        co_return std::move(*evaluated);
    }
    if (const auto failure = draft.diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    invariant_violation("constant root execution failed without a diagnostic");
}

auto evaluate_constant_body(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    ConstantBodyRoot root
) noexcept -> AnalysisTask<void> {
    auto context = ConstantAnalysisContext(draft, requests, root);
    const auto result =
        (co_await execute_body(draft, context, ExecutionBody(draft.body_draft(root.body))));
    if (!result) {
        if (const auto failure = draft.diagnostics().failure()) {
            co_return std::unexpected(*failure);
        }
        invariant_violation("constant body execution failed without a diagnostic");
    }
    co_return {};
}
