module carven:semantic.analysis.constant.admission.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.admission;
import :semantic.analysis.operations;
import :semantic.evaluation.admission;
import :semantic.semir.body;
import :semantic.semir.children;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.generic;
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
    auto supported_type(ConstructionTypeRef type) const noexcept -> bool;
    auto check_function(FunctionID function) noexcept -> void;
    auto check_generic_function(GenericDeclarationID definition) noexcept -> void;
    auto check_contract(
        const ConstructionCallableContract& contract,
        ProgramOriginID origin
    ) noexcept -> bool;
    auto check_body(const StructuredBodyDraft& body) noexcept -> void;
    auto check_call(std::optional<CallableID> target, ProgramOriginID origin) noexcept -> void;
    auto check_call(GenericDeclarationID target, ProgramOriginID origin) noexcept -> void;

    ProgramDraft& draft;
    std::span<const std::optional<BodyID>> function_bodies;
    std::optional<ProgramOriginID> contract_origin;
    std::optional<AnalysisFailure> failure;

    struct PendingFailures final {
        ConstructionFailureRef source;
        ProgramOriginID origin;
        ProgramOriginID contract;
    };

    std::vector<PendingFailures> pending_failures;
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
        const auto* function = std::get_if<GenericFunctionDefinition>(&source);
        if (!function || !function->is_const) {
            continue;
        }
        contract_origin = function->contract.origin;
        check_generic_function(id);
        if (failure) {
            return std::unexpected(*failure);
        }
    }
    if (!pending_failures.empty()) {
        auto effects = draft.preview_failures();
        if (!effects) {
            return std::unexpected(effects.error());
        }
        for (const auto& pending : pending_failures) {
            contract_origin = pending.contract;
            if (std::ranges::any_of(
                    effects->known_members(pending.source),
                    [&](const auto type) noexcept { return !supported_type(type); }
                )) {
                reject(
                    pending.origin,
                    "function signature uses a value without compile-time execution support"
                );
                return std::unexpected(*failure);
            }
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

auto ConstFunctionValidator::supported_type(ConstructionTypeRef type) const noexcept -> bool {
    auto pending = std::vector<ConstructionTypeRef> {type};
    auto visited = std::set<ConstructionTypeRef>();
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        if (std::holds_alternative<TypeID>(next)) {
            if (!supported_execution_type(draft, next)) {
                return false;
            }
            continue;
        }
        if (!visited.insert(next).second) {
            continue;
        }
        if (const auto* generic = std::get_if<GenericTypeID>(&next)) {
            const auto shape = draft.generic_type_copy(*generic);
            const auto admitted = shape.visit([&](const auto& value) noexcept -> bool {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, GenericCppNamedType>
                              || std::same_as<Value, GenericCppQueryType>) {
                    return false;
                } else if constexpr (std::same_as<Value, TypeID>) {
                    pending.emplace_back(value);
                } else if constexpr (std::same_as<Value, GenericArrayType>) {
                    pending.emplace_back(value.element);
                } else if constexpr (std::same_as<Value, GenericTupleType>) {
                    for (const auto element : value.elements) {
                        pending.emplace_back(element);
                    }
                } else if constexpr (std::same_as<Value, GenericNominalApplication>) {
                    const auto definition = draft.generic_declaration_copy(value.definition);
                    definition.visit([&](const auto& source) noexcept {
                        using Definition = std::remove_cvref_t<decltype(source)>;
                        const auto field = [&](GenericTypeID field) noexcept {
                            pending.emplace_back(draft.substitute_generic_type(
                                field,
                                value.definition,
                                value.arguments
                            ));
                        };
                        if constexpr (std::same_as<Definition, GenericRecordDefinition>) {
                            for (const auto& member : source.fields) {
                                field(member.type);
                            }
                        } else if constexpr (std::same_as<Definition, GenericEnumDefinition>) {
                            for (const auto& member : source.cases) {
                                for (const auto type : member.payload_types) {
                                    field(type);
                                }
                            }
                        }
                    });
                } else if constexpr (std::same_as<Value, GenericVectorType>) {
                    pending.emplace_back(value.element);
                }
                // Rigid holders carry an argument value; no copy/default/equality
                // capability is inferred. Views and deferred owners do not execute
                // or freeze the storage of their targets at this boundary.
                return true;
            });
            if (!admitted) {
                return false;
            }
            continue;
        }
        const auto term = std::get<TypeTermID>(next);
        const auto construction = draft.construction_type_copy(term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            pending.push_back(array->element);
        } else if (const auto* tuple =
                       std::get_if<ConstructionTupleTypeValue>(&construction.value)) {
            for (const auto element : tuple->elements) {
                pending.push_back(element);
            }
        } else if (std::holds_alternative<ConstructionOperationTypeValue>(construction.value)) {
            // The descriptor is local to execution; freezing separately rejects it.
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

auto ConstFunctionValidator::check_contract(
    const ConstructionCallableContract& contract,
    ProgramOriginID origin
) noexcept -> bool {
    if (!supported_type(contract.result)
        || std::ranges::any_of(contract.parameters, [&](const auto& parameter) noexcept {
               return !supported_type(parameter.type);
           })) {
        reject(origin, "function signature uses a value without compile-time execution support");
        return false;
    }
    // Type traversal can form dependent descriptors. Once it is complete, one
    // shared failure solution checks every const contract in this batch.
    pending_failures.push_back(
        {.source = contract.failures, .origin = origin, .contract = *contract_origin}
    );
    return true;
}

auto ConstFunctionValidator::check_generic_function(GenericDeclarationID definition) noexcept
    -> void {
    const auto source =
        std::get<GenericFunctionDefinition>(draft.generic_declaration_copy(definition));
    if (!check_contract(source.signature, source.contract.origin)) {
        return;
    }
    if (!draft.has_generic_function_body(definition)) {
        reject(source.contract.origin, "generic function has no checked compile-time body");
        return;
    }
    check_body(draft.generic_function_body(definition));
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
    if (!check_contract(contract, declaration.origin)) {
        return;
    }
    auto body = function.index() < function_bodies.size()
        ? function_bodies[function.index()]
        : draft.body_for_callable(declaration.callable);
    if (!body) {
        reject(declaration.origin, "native function has no compile-time provider");
        return;
    }
    check_body(draft.body_draft(*body));
}

auto ConstFunctionValidator::check_call(
    std::optional<CallableID> target,
    ProgramOriginID origin
) noexcept -> void {
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

auto ConstFunctionValidator::check_call(
    GenericDeclarationID target,
    ProgramOriginID origin
) noexcept -> void {
    const auto source = draft.generic_declaration_copy(target);
    const auto* function = std::get_if<GenericFunctionDefinition>(&source);
    if (!function || !function->is_const) {
        reject(origin, "const fn can only call an explicitly declared const fn");
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
                    if (const auto reason = unsupported_execution_statement(*statement)) {
                        reject(statement->origin, std::string(*reason));
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
                    // Control operations select a child result rather than construct
                    // their contextual result type. Check the reachable producers;
                    // a noncompleting branch need not produce that type at all.
                    const auto selects_result = std::holds_alternative<SemIf>(expression->value)
                        || std::holds_alternative<SemMatch>(expression->value)
                        || std::holds_alternative<SemTry>(expression->value);
                    if (expression->operation_reachable
                        && !selects_result
                        && !supported_type(expression->type.construction())) {
                        reject(
                            expression->origin,
                            "expression type is not supported in compile-time execution"
                        );
                        return;
                    }
                    if (expression->operation_reachable) {
                        if (std::holds_alternative<SemColdTimeout>(expression->value)) {
                            reject(expression->origin, "timeout requires a native clock");
                            return;
                        }
                        const auto reason =
                            std::holds_alternative<SemGenericCall>(expression->value)
                            ? std::optional<std::string_view>()
                            : unsupported_execution_expression(*expression);
                        if (reason) {
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
                                check_call(operation.target, expression->origin);
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
