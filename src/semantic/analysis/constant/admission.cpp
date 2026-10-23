module carven:semantic.analysis.constant.admission.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.admission;
import :semantic.analysis.operations;
import :semantic.evaluation.admission;
import :semantic.semir.body;
import :semantic.semir.children;
import :semantic.semir.decl;
import :semantic.semir.generic;
import :semantic.semir.initialization;
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
    auto check_function(FunctionID function) noexcept -> void;
    auto check_generic_function(GenericDeclarationID definition) noexcept -> void;
    auto check_body(const StructuredBodyDraft& body) noexcept -> void;
    auto check_call(CallableID target, ProgramOriginID origin) noexcept -> void;
    auto check_call(GenericTypeID target, ProgramOriginID origin) noexcept -> void;
    auto check_call(
        std::optional<ConstructionCallableRef> target,
        const SemanticExpression& callee,
        ProgramOriginID origin
    ) noexcept -> void;
    auto check_call(GenericDeclarationID target, ProgramOriginID origin) noexcept -> void;

    ProgramDraft& draft;
    std::span<const std::optional<BodyID>> function_bodies;
    std::optional<ProgramOriginID> contract_origin;
    std::optional<AnalysisFailure> failure;

    std::set<ConstructionCallableRef> checked_closures;
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
        contract_origin = draft.function_declaration_copy(id).origin;
        check_function(id);
        if (failure) {
            return std::unexpected(*failure);
        }
    }
    for (const auto id : draft.generic_declaration_ids()) {
        const auto source = draft.generic_declaration_copy(id);
        const auto* function = std::get_if<GenericCallableDefinition>(&source);
        if (!function || !function->contract.name || !function->is_const) {
            continue;
        }
        contract_origin = function->contract.origin;
        check_generic_function(id);
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
    if (*contract_origin != origin) {
        diagnostic.related(
            draft.source_span(*contract_origin),
            "const fn promises compile-time capability"
        );
    }
    failure = draft.diagnostics().error(diagnostic.build());
}

auto ConstFunctionValidator::check_generic_function(GenericDeclarationID definition) noexcept
    -> void {
    const auto source =
        std::get<GenericCallableDefinition>(draft.generic_declaration_copy(definition));
    if (!draft.has_generic_callable_body(definition)) {
        reject(source.contract.origin, "generic function has no checked compile-time body");
        return;
    }
    check_body(draft.generic_callable_body(definition));
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
    if (const auto implementation = draft.callable_implementation_copy(declaration.callable);
        implementation && std::holds_alternative<CppImportImplementation>(*implementation)) {
        return;
    }
    auto body = function.index() < function_bodies.size() ? function_bodies[function.index()]
                                                          : std::nullopt;
    if (!body) {
        body = draft.body_for_callable(declaration.callable);
    }
    if (!body) {
        reject(declaration.origin, "native function has no compile-time provider");
        return;
    }
    check_body(draft.body_draft(*body));
}

auto ConstFunctionValidator::check_call(CallableID target, ProgramOriginID origin) noexcept
    -> void {
    if (const auto implementation = draft.callable_implementation_copy(target); implementation
        && (std::holds_alternative<NumberParseImplementation>(*implementation)
            || std::holds_alternative<ProvidedFunctionImplementation>(*implementation))) {
        return;
    }
    if (const auto implementation = draft.callable_implementation_copy(target)) {
        if (const auto* closure = std::get_if<ClosureBodyImplementation>(&*implementation)) {
            if (checked_closures.emplace(target).second) {
                check_body(draft.body_draft(closure->body));
            }
            return;
        }
    }
    const auto function = draft.function_for_callable(target);
    if (!function) {
        reject(origin, "call target has no compile-time provider");
        return;
    }
    if (!draft.function_declaration_copy(*function).is_const) {
        reject(origin, "const fn can only call an explicitly declared const fn");
        return;
    }
}

auto ConstFunctionValidator::check_call(
    GenericDeclarationID target,
    ProgramOriginID origin
) noexcept -> void {
    const auto source = draft.generic_declaration_copy(target);
    const auto* function = std::get_if<GenericCallableDefinition>(&source);
    if (!function || !function->is_const) {
        reject(origin, "const fn can only call an explicitly declared const fn");
    }
}

auto ConstFunctionValidator::check_call(GenericTypeID target, ProgramOriginID) noexcept -> void {
    const auto shape = draft.generic_type_copy(target);
    const auto& closure = std::get<GenericClosureType>(shape);
    if (checked_closures.emplace(target).second) {
        check_body(draft.generic_callable_body(closure.definition));
    }
}

auto ConstFunctionValidator::check_call(
    std::optional<ConstructionCallableRef> target,
    const SemanticExpression& callee,
    ProgramOriginID origin
) noexcept -> void {
    if (!target) {
        const auto type = callee.type.construction();
        if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
            if (std::holds_alternative<GenericClosureType>(draft.generic_type_copy(*generic))) {
                target = *generic;
            }
        } else if (const auto* concrete = std::get_if<TypeID>(&type)) {
            const auto shape = draft.type_copy(*concrete);
            if (const auto* closure = std::get_if<ClosureTypeValue>(&shape.value)) {
                target = closure->callable;
            }
        }
    }
    if (!target) {
        reject(origin, "cannot prove compile-time capability of indirect call target");
        return;
    }
    target->visit([&](const auto callable) noexcept { check_call(callable, origin); });
}

auto ConstFunctionValidator::check_body(const StructuredBodyDraft& body) noexcept -> void {
    auto nodes = std::vector<PendingNode> {&body.region};
    while (!nodes.empty() && !failure) {
        const auto next = nodes.back();
        nodes.pop_back();
        std::visit(
            Overloaded {
                [&](const SemanticRegion* region) noexcept {
                    const auto first = nodes.size();
                    for (const auto& statement : region->statements) {
                        if (statement.reachable) {
                            nodes.emplace_back(&statement);
                        }
                    }
                    if (region->result && region->result_reachable) {
                        nodes.emplace_back(&*region->result);
                    }
                    std::reverse(nodes.begin() + static_cast<std::ptrdiff_t>(first), nodes.end());
                },
                [&](const SemanticStatement* statement) noexcept {
                    if (std::holds_alternative<SemStaticBinding>(statement->value)
                        || std::holds_alternative<SemConstBlock>(statement->value)) {
                        return;
                    }
                    const auto first = nodes.size();
                    statement->value.visit([&](const auto& operation) noexcept {
                        visit_evaluation_children(operation, [&](const auto& child) noexcept {
                            using Child = std::remove_cvref_t<decltype(child)>;
                            if constexpr (std::same_as<Child, SemanticExpression>) {
                                nodes.emplace_back(&child);
                            } else if constexpr (std::same_as<Child, SemanticRegion>) {
                                nodes.emplace_back(&child);
                            }
                        });
                    });
                    std::reverse(nodes.begin() + static_cast<std::ptrdiff_t>(first), nodes.end());
                },
                [&](const SemanticExpression* expression) noexcept {
                    if (expression->operation_reachable) {
                        if (std::holds_alternative<SemDefault>(expression->value)
                            && default_initialization(draft, expression->type.construction())
                                == DefaultInitialization::Native) {
                            reject(
                                expression->origin,
                                "native default initialization has no compile-time implementation"
                            );
                            return;
                        }
                        if (const auto reason = unsupported_execution_expression(*expression)) {
                            reject(expression->origin, std::string(*reason));
                            return;
                        }
                    }
                    const auto first = nodes.size();
                    expression->value.visit([&](const auto& operation) noexcept {
                        visit_evaluation_children(operation, [&](const auto& child) noexcept {
                            using Child = std::remove_cvref_t<decltype(child)>;
                            if constexpr (std::same_as<Child, SemanticExpression>) {
                                nodes.emplace_back(&child);
                            } else if constexpr (std::same_as<Child, SemanticRegion>) {
                                nodes.emplace_back(&child);
                            }
                        });
                        using Operation = std::remove_cvref_t<decltype(operation)>;
                        if constexpr (std::same_as<Operation, SemCall>
                                      || std::same_as<Operation, SemColdCall>) {
                            if (expression->operation_reachable) {
                                check_call(operation.target, *operation.callee, expression->origin);
                            }
                        } else if constexpr (std::same_as<Operation, SemGenericCall>) {
                            if (expression->operation_reachable) {
                                check_call(operation.definition, expression->origin);
                            }
                        }
                    });
                    std::reverse(nodes.begin() + static_cast<std::ptrdiff_t>(first), nodes.end());
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
