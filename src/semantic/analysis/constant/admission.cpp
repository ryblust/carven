module carven:semantic.analysis.constant.admission.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.admission;
import :semantic.evaluation.admission;
import :semantic.evaluation.shape;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

class ConstantBodyAdmission final {
public:
    ConstantBodyAdmission(
        ProgramDraft& draft,
        std::optional<FunctionID> function,
        const StructuredBodyDraft& body
    ) noexcept;
    auto run() noexcept -> AnalysisResult<void>;

private:
    auto reject(ProgramOriginID origin, std::string message) noexcept -> void;
    auto supported_type(ConstructionTypeRef type, bool allow_void = false) const noexcept -> bool;
    auto expression(const SemanticExpression& source) noexcept -> void;
    auto statement(const SemanticStatement& source) noexcept -> void;
    auto call(const SemCall& value, ProgramOriginID origin) noexcept -> void;

    ExecutionTypeShapes shapes;
    ProgramDraft& draft;
    std::optional<FunctionID> function;
    const StructuredBodyDraft& body;
    std::optional<AnalysisFailure> failure;
    std::set<const SemanticExpression*> direct_callees;
};

ConstantBodyAdmission::ConstantBodyAdmission(
    ProgramDraft& draft,
    std::optional<FunctionID> function,
    const StructuredBodyDraft& body
) noexcept
    : shapes(draft),
      draft(draft),
      function(function),
      body(body) {}

auto ConstantBodyAdmission::reject(ProgramOriginID origin, std::string message) noexcept -> void {
    if (failure) {
        return;
    }
    auto diagnostic = DiagnosticBuilder(DiagnosticCode::ConstAdmission, std::move(message));
    diagnostic.primary(draft.source_span(origin));
    failure = draft.diagnostics().error(diagnostic.build());
}

auto ConstantBodyAdmission::supported_type(ConstructionTypeRef type, bool allow_void) const noexcept
    -> bool {
    return supported_execution_type(draft, shapes, type, allow_void);
}

auto ConstantBodyAdmission::run() noexcept -> AnalysisResult<void> {
    if (function) {
        const auto declaration = draft.function_declaration_copy(*function);
        if (!declaration.is_const) {
            return {};
        }
        if (draft.pending_function_contract_copy(declaration.callable)) {
            reject(declaration.origin, "const fn requires a completed concrete signature");
            return std::unexpected(*failure);
        }
        const auto contract = draft.construction_callable_contract_copy(declaration.callable);
        if (!supported_type(contract.result, true)) {
            reject(
                declaration.origin,
                "const fn result requires a supported scalar, text, aggregate, or void type"
            );
        }
        for (const auto& parameter : contract.parameters) {
            if (parameter.access == AccessMode::Write) {
                reject(declaration.origin, "const fn parameters cannot use Write access");
            } else if (!supported_type(parameter.type)) {
                reject(
                    declaration.origin,
                    "const fn parameters require supported scalar, text, or aggregate values"
                );
            }
        }
        if (!draft.construction_failure_term_copy(contract.failures).direct_members.empty()) {
            reject(declaration.origin, "const fn cannot declare typed failures");
        }
    }
    for (const auto entry : body.bindings.entries()) {
        if (!supported_type(entry.value.type)) {
            reject(
                entry.value.origin,
                "constant execution local values require supported scalar, text, or aggregate types"
            );
        }
    }
    for (const auto entry : body.patterns.entries()) {
        if (!supported_type(entry.value.type) || !supported_execution_pattern(entry.value.value)) {
            reject(
                entry.value.origin,
                "constant execution match requires supported values and literal, binding, wildcard, or alternative patterns"
            );
        }
    }
    visit_semantic_nodes(
        body.region,
        Overloaded {
            [&](const SemanticExpression& source) noexcept { expression(source); },
            [&](const SemanticStatement& source) noexcept { statement(source); },
        }
    );
    return failure ? AnalysisResult<void>(std::unexpected(*failure)) : AnalysisResult<void>();
}

auto ConstantBodyAdmission::call(const SemCall& value, ProgramOriginID origin) noexcept -> void {
    const auto* selected = std::get_if<SemCallable>(&value.callee->value);
    const auto callee =
        selected == nullptr ? std::nullopt : draft.function_for_callable(selected->callable);
    if (!callee || !draft.function_declaration_copy(*callee).is_const) {
        reject(origin, "constant execution calls must directly select a const fn");
        return;
    }
    direct_callees.insert(std::addressof(*value.callee));
    for (const auto& argument : value.arguments) {
        if (argument.access == AccessMode::Write) {
            reject(
                argument.expression.origin,
                "constant execution calls cannot pass Write arguments"
            );
        }
    }
}

auto ConstantBodyAdmission::expression(const SemanticExpression& source) noexcept -> void {
    if (failure) {
        return;
    }
    if (std::holds_alternative<SemCallable>(source.value)) {
        if (!direct_callees.contains(std::addressof(source))) {
            reject(source.origin, "constant execution cannot form callable values");
        }
        return;
    }
    if (!supported_type(source.type.construction(), true)) {
        reject(source.origin, "expression type is not supported in constant execution");
        return;
    }
    if (const auto reason = unsupported_execution_expression(source)) {
        reject(source.origin, std::string(*reason));
    } else if (const auto* selected = std::get_if<SemCall>(&source.value)) {
        call(*selected, source.origin);
    }
}

auto ConstantBodyAdmission::statement(const SemanticStatement& source) noexcept -> void {
    if (failure) {
        return;
    }
    if (const auto reason = unsupported_execution_statement(source)) {
        reject(source.origin, std::string(*reason));
    }
}

} // namespace

auto validate_constant_function(
    ProgramDraft& draft,
    FunctionID function,
    const StructuredBodyDraft& body
) noexcept -> AnalysisResult<void> {
    return ConstantBodyAdmission(draft, function, body).run();
}

auto validate_constant_test(ProgramDraft& draft, const StructuredBodyDraft& body) noexcept
    -> AnalysisResult<void> {
    return ConstantBodyAdmission(draft, std::nullopt, body).run();
}
