module carven:semantic.analysis.constant.admission.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.admission;
import :semantic.evaluation.admission;
import :semantic.semir.body;
import :semantic.semir.children;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

using PendingNode =
    std::variant<const SemanticRegion*, const SemanticStatement*, const SemanticExpression*>;

class ConstFunctionValidator final {
public:
    ConstFunctionValidator(
        ProgramDraft& draft,
        std::span<const std::optional<BodyID>> function_bodies
    ) noexcept;
    auto run() noexcept -> AnalysisResult<void>;

private:
    auto reject(ProgramOriginID origin, std::string message) noexcept -> void;
    auto supported_type(ConstructionTypeRef type, bool allow_void = false) const noexcept -> bool;
    auto check_function(FunctionID function) noexcept -> void;
    auto check_body(const StructuredBodyDraft& body) noexcept -> void;
    auto check_call(const SemCall& operation, ProgramOriginID origin) noexcept -> void;

    ProgramDraft& draft;
    std::span<const std::optional<BodyID>> function_bodies;
    std::optional<FunctionID> current_function;
    std::optional<AnalysisFailure> failure;
};

ConstFunctionValidator::ConstFunctionValidator(
    ProgramDraft& draft,
    std::span<const std::optional<BodyID>> function_bodies
) noexcept
    : draft(draft),
      function_bodies(function_bodies) {}

auto ConstFunctionValidator::run() noexcept -> AnalysisResult<void> {
    for (const auto id : draft.function_declaration_ids()) {
        if (!draft.function_declaration_copy(id).is_const) {
            continue;
        }
        current_function = id;
        check_function(id);
        if (failure) {
            return std::unexpected(*failure);
        }
    }
    return {};
}

auto ConstFunctionValidator::reject(ProgramOriginID origin, std::string message) noexcept -> void {
    if (failure) {
        return;
    }
    auto diagnostic = DiagnosticBuilder(DiagnosticCode::ConstAdmission, std::move(message));
    diagnostic.primary(draft.source_span(origin));
    const auto declaration = draft.function_declaration_copy(*current_function);
    if (declaration.origin != origin) {
        diagnostic.related(
            draft.source_span(declaration.origin),
            "const fn promises compile-time capability"
        );
    }
    failure = draft.diagnostics().error(diagnostic.build());
}

auto ConstFunctionValidator::supported_type(
    ConstructionTypeRef type,
    bool allow_void
) const noexcept -> bool {
    auto pending = std::vector<std::pair<ConstructionTypeRef, bool>> {{type, allow_void}};
    auto visited = std::set<TypeTermID>();
    while (!pending.empty()) {
        const auto [next, void_allowed] = pending.back();
        pending.pop_back();
        if (std::holds_alternative<TypeID>(next)) {
            if (!supported_execution_type(draft, next, void_allowed)) {
                return false;
            }
            continue;
        }
        const auto term = std::get<TypeTermID>(next);
        if (!visited.insert(term).second) {
            continue;
        }
        const auto construction = draft.construction_type_copy(term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            pending.emplace_back(array->element, false);
        } else if (std::holds_alternative<ConstructionSliceTypeValue>(construction.value)
                   || std::holds_alternative<ConstructionCallableViewTypeValue>(
                       construction.value
                   )) {
            // Views do not contain their targets' storage.
        } else {
            return false;
        }
    }
    return true;
}

auto ConstFunctionValidator::check_function(FunctionID function) noexcept -> void {
    const auto declaration = draft.function_declaration_copy(function);
    if (draft.pending_function_contract_copy(declaration.callable)) {
        reject(
            declaration.origin,
            "cannot prove compile-time capability of an unresolved function signature"
        );
        return;
    }
    const auto contract = draft.construction_callable_contract_copy(declaration.callable);
    if (!supported_type(contract.result, true)
        || std::ranges::any_of(
            contract.parameters,
            [&](const auto& parameter) noexcept { return !supported_type(parameter.type); }
        )
        || std::ranges::any_of(
            draft.construction_failure_term_copy(contract.failures).direct_members,
            [&](const auto type) noexcept { return !supported_type(type); }
        )) {
        reject(
            declaration.origin,
            "function signature uses a value without compile-time execution support"
        );
        return;
    }
    if (function.index() >= function_bodies.size() || !function_bodies[function.index()]) {
        reject(declaration.origin, "native function has no compile-time provider");
        return;
    }
    check_body(draft.body_draft(*function_bodies[function.index()]));
}

auto ConstFunctionValidator::check_call(const SemCall& operation, ProgramOriginID origin) noexcept
    -> void {
    const auto target = operation.target;
    if (!target) {
        reject(origin, "cannot prove compile-time capability of indirect call target");
        return;
    }
    const auto function = draft.function_for_callable(*target);
    if (!function) {
        reject(origin, "call target has no compile-time provider");
        return;
    }
    if (!draft.function_declaration_copy(*function).is_const) {
        reject(origin, "const fn can only call an explicitly declared const fn");
        return;
    }
}

auto ConstFunctionValidator::check_body(const StructuredBodyDraft& body) noexcept -> void {
    auto nodes = std::vector<PendingNode> {&body.region};
    while (!nodes.empty() && !failure) {
        const auto next = nodes.back();
        nodes.pop_back();
        std::visit(
            Overloaded {
                [&](const SemanticRegion* region) noexcept {
                    auto children = std::vector<PendingNode>();
                    for (const auto& statement : region->statements) {
                        if (statement.reachable) {
                            children.emplace_back(&statement);
                        }
                    }
                    if (region->result && region->result_reachable) {
                        children.emplace_back(&*region->result);
                    }
                    nodes.insert(nodes.end(), children.rbegin(), children.rend());
                },
                [&](const SemanticStatement* statement) noexcept {
                    if (std::holds_alternative<SemStaticBinding>(statement->value)
                        || std::holds_alternative<SemConstBlock>(statement->value)) {
                        return;
                    }
                    if (const auto reason = unsupported_execution_statement(*statement)) {
                        reject(statement->origin, std::string(*reason));
                        return;
                    }
                    auto children = std::vector<PendingNode>();
                    statement->value.visit([&](const auto& operation) noexcept {
                        visit_evaluation_children(operation, [&](const auto& child) noexcept {
                            using Child = std::remove_cvref_t<decltype(child)>;
                            if constexpr (std::same_as<Child, SemanticExpression>) {
                                children.emplace_back(&child);
                            } else if constexpr (std::same_as<Child, SemanticRegion>) {
                                children.emplace_back(&child);
                            }
                        });
                    });
                    nodes.insert(nodes.end(), children.rbegin(), children.rend());
                },
                [&](const SemanticExpression* expression) noexcept {
                    // Control operations select a child result rather than construct
                    // their contextual result type. Check the reachable producers;
                    // a noncompleting branch need not produce that type at all.
                    const auto selects_result = std::holds_alternative<SemIf>(expression->value)
                        || std::holds_alternative<SemMatch>(expression->value)
                        || std::holds_alternative<SemTry>(expression->value);
                    if (expression->operation_reachable
                        && !selects_result
                        && !supported_type(expression->type.construction(), true)) {
                        reject(
                            expression->origin,
                            "expression type is not supported in compile-time execution"
                        );
                        return;
                    }
                    if (expression->operation_reachable) {
                        if (const auto* match = std::get_if<SemMatch>(&expression->value)) {
                            for (const auto& arm : match->arms) {
                                if (arm.reachable
                                    && std::ranges::any_of(
                                        arm.bindings,
                                        [&](LocalBindingID binding) noexcept {
                                            return std::holds_alternative<AliasBindingStorage>(
                                                body.bindings.get(binding).storage
                                            );
                                        }
                                    )) {
                                    reject(
                                        expression->origin,
                                        "borrowed pattern bindings are not supported in compile-time execution"
                                    );
                                    return;
                                }
                            }
                        }
                        if (const auto reason = unsupported_execution_expression(*expression)) {
                            reject(expression->origin, std::string(*reason));
                            return;
                        }
                    }
                    auto children = std::vector<PendingNode>();
                    expression->value.visit([&](const auto& operation) noexcept {
                        visit_evaluation_children(operation, [&](const auto& child) noexcept {
                            using Child = std::remove_cvref_t<decltype(child)>;
                            if constexpr (std::same_as<Child, SemanticExpression>) {
                                children.emplace_back(&child);
                            } else if constexpr (std::same_as<Child, SemanticRegion>) {
                                children.emplace_back(&child);
                            }
                        });
                        using Operation = std::remove_cvref_t<decltype(operation)>;
                        if constexpr (std::same_as<Operation, SemCall>) {
                            if (expression->operation_reachable) {
                                check_call(operation, expression->origin);
                            }
                        }
                    });
                    nodes.insert(nodes.end(), children.rbegin(), children.rend());
                },
            },
            next
        );
    }
}

} // namespace

auto validate_const_contracts(
    ProgramDraft& draft,
    std::span<const std::optional<BodyID>> function_bodies
) noexcept -> AnalysisResult<void> {
    return ConstFunctionValidator(draft, function_bodies).run();
}
